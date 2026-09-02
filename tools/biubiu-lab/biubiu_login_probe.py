#!/usr/bin/env python3
"""Clean-room probe for biubiu's desktop account login envelope."""

import argparse
import base64
import getpass
import json
import os
from pathlib import Path
import string
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding as rsa_padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


LOGIN_ORIGIN = "https://member-login.biubiu001.com/"
PUBLIC_KEY_DER_B64 = (
    "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDCjYYIy9Are9QPRDOVug4e6Fdz"
    "8HK2HGyajKR4N8Wb/bB9gwXnieXqj4Mya0nLd6nBcBPN6qUJ0R7p5Cv6aPqQsc7"
    "pWfAxPr41GvcOlGixLtpLHLUH9m0093YEBhu4F7pKu0TZTQIPZINWUa1SLjQD/bc"
    "BlcaQyWbk6qJhSJFYkwIDAQAB"
)
ALPHABET = (string.ascii_uppercase + string.ascii_lowercase + string.digits).encode()


def random_ascii(length: int = 16) -> bytes:
    raw = os.urandom(length)
    return bytes(ALPHABET[value % len(ALPHABET)] for value in raw)


def biubiu_pad(plaintext: bytes) -> bytes:
    count = 16 - ((len(plaintext) + 1) % 16)
    return plaintext + b"\x0a" + bytes([count]) * count


def biubiu_unpad(plaintext: bytes) -> bytes:
    if not plaintext:
        raise ValueError("empty encrypted response")
    count = plaintext[-1]
    if count < 1 or count > 16 or plaintext[-count:] != bytes([count]) * count:
        raise ValueError("invalid response padding")
    value = plaintext[:-count]
    if value.endswith(b"\x0a"):
        value = value[:-1]
    return value


def encrypt_payload(payload: dict) -> tuple[dict, bytes]:
    session_key = random_ascii()
    plaintext = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode()
    encryptor = Cipher(algorithms.AES(session_key), modes.CBC(session_key)).encryptor()
    ciphertext = encryptor.update(biubiu_pad(plaintext)) + encryptor.finalize()
    public_key = serialization.load_der_public_key(base64.b64decode(PUBLIC_KEY_DER_B64))
    encrypted_key = public_key.encrypt(session_key, rsa_padding.PKCS1v15())
    encrypted_key_b64 = base64.b64encode(encrypted_key).decode()
    return {
        "k": encrypted_key_b64,
        "v": 1,
        "d": base64.b64encode(ciphertext).decode(),
        "i": encrypted_key_b64,
    }, session_key


def decrypt_payload(ciphertext_b64: str, session_key: bytes):
    ciphertext = base64.b64decode(ciphertext_b64)
    decryptor = Cipher(algorithms.AES(session_key), modes.CBC(session_key)).decryptor()
    plaintext = biubiu_unpad(decryptor.update(ciphertext) + decryptor.finalize())
    value = json.loads(plaintext)
    if isinstance(value, str):
        value = json.loads(value)
    return value


def client_context(device_id: str) -> dict:
    return {
        "clientDevice": {
            "appVer": "100843982",
            "clientFlag": 0,
            "deviceId": device_id,
            "utdid": device_id,
            "umid": "",
            "userAgent": (
                "Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/59.0 Safari/537.36 biubiu/windows/8.4.3 windows"
            ),
            "clientBizId": "biubiu",
            "clientAppCode": "BIUBIU_DESKTOP",
            "clientType": "DESKTOP",
            "os": "linux",
            "osVer": "openwrt",
            "sdkVer": "100843982",
        },
        "clientUser": {"sessionId": ""},
        "clientScene": {
            "clientCaller": "biubiu-sdk-windows",
            "bizId": "biubiu",
            "appCode": "BIUBIU_DESKTOP",
            "channel": "official",
        },
    }


def request(endpoint: str, payload: dict) -> dict:
    request_id = str(uuid.uuid4())
    envelope, session_key = encrypt_payload(payload)
    envelope["requestId"] = request_id
    body = json.dumps(envelope, separators=(",", ":")).encode()
    req = urllib.request.Request(
        LOGIN_ORIGIN + endpoint,
        data=body,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20) as response:
        outer = json.load(response)
    encrypted = outer.get("d")
    if not isinstance(encrypted, str):
        return outer
    return decrypt_payload(encrypted, session_key)


def atomic_private_write(path: Path, data: bytes) -> None:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            prefix=f".{path.name}.",
            dir=path.parent,
            delete=False,
        ) as handle:
            temporary = Path(handle.name)
            os.fchmod(handle.fileno(), 0o600)
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        temporary.replace(path)
    except Exception:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        raise


def resolve_device_id(explicit: str | None, path: Path) -> str:
    if explicit:
        return str(uuid.UUID(explicit))
    try:
        return str(uuid.UUID(path.read_text().strip()))
    except FileNotFoundError:
        device_id = str(uuid.uuid4())
        atomic_private_write(path, (device_id + "\n").encode())
        return device_id


def store_session(session_file: Path, device_id: str, method: str, result: dict) -> None:
    if result.get("code") != "SUCCESS":
        raise RuntimeError(result.get("msg") or f"{method} login failed")
    atomic_private_write(
        session_file,
        json.dumps(
            {"local": {"deviceId": device_id, "method": method}, "login": result},
            ensure_ascii=False,
            indent=2,
        ).encode(),
    )


def load_session(session_file: Path) -> tuple[dict, str, dict]:
    record = json.loads(session_file.read_text())
    device_id = str(uuid.UUID(record["local"]["deviceId"]))
    login = record.get("login")
    if not isinstance(login, dict):
        raise ValueError("session file has no login response")
    session_info = ((login.get("data") or {}).get("sessionInfo") or {})
    if not isinstance(session_info, dict):
        raise ValueError("session file has invalid sessionInfo")
    return record, device_id, session_info


def session_status(session_file: Path) -> dict:
    record, _, session_info = load_session(session_file)
    cookies = session_info.get("cookies")
    return {
        "authenticated": bool(session_info.get("sessionId")),
        "refreshable": bool(session_info.get("refreshToken")),
        "cookieCount": len(cookies) if isinstance(cookies, list) else 0,
        "method": (record.get("local") or {}).get("method", "unknown"),
        "deviceIdStored": True,
    }


def refresh_session(session_file: Path) -> None:
    _, device_id, session_info = load_session(session_file)
    refresh_token = session_info.get("refreshToken")
    session_id = session_info.get("sessionId")
    if not isinstance(refresh_token, str) or not refresh_token:
        raise ValueError("session file has no refresh token")
    if not isinstance(session_id, str) or not session_id:
        raise ValueError("session file has no session ID")
    payload = client_context(device_id)
    payload["clientUser"]["sessionId"] = session_id
    payload["sessionToken"] = refresh_token
    result = request("capi/login.autoLogin", payload)
    store_session(session_file, device_id, "refresh", result)


def authorize(device_id: str, qr_file: Path, session_file: Path) -> int:
    payload = client_context(device_id)
    payload.update({"qrCodeScene": 1, "loginAuthUrl": "pages/Home/index"})
    started = request("capi/qrcodelogin.startQRCodeLogin", payload)
    data = started.get("data") or {}
    qr_data = data.get("qrCodeBase64", "")
    token = data.get("qrToken", "")
    if not qr_data.startswith("data:image/png;base64,") or not token:
        raise RuntimeError("server did not return a QR login challenge")
    atomic_private_write(qr_file, base64.b64decode(qr_data.split(",", 1)[1]))
    print(f"QR_READY {qr_file}", flush=True)

    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        time.sleep(1)
        poll_payload = client_context(device_id)
        poll_payload["qrToken"] = token
        polled = request("capi/qrcodelogin.queryLoginStatus", poll_payload)
        poll_data = polled.get("data") or {}
        status = poll_data.get("status")
        if status == 4:
            raise RuntimeError("QR login challenge expired")
        code = poll_data.get("vcode")
        if not code:
            continue
        login_payload = client_context(device_id)
        login_payload["connectCode"] = code
        logged_in = request("capi/login.autoLoginByCode", login_payload)
        store_session(session_file, device_id, "qr", logged_in)
        print(f"LOGIN_SUCCESS {session_file}", flush=True)
        return 0
    raise RuntimeError("QR login challenge timed out")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id")
    parser.add_argument(
        "--device-id-file", type=Path, default=Path("/tmp/biubiu-device-id")
    )
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--authorize", action="store_true")
    actions.add_argument("--send-sms", metavar="PHONE")
    actions.add_argument("--sms-login", nargs=2, metavar=("PHONE", "CODE"))
    actions.add_argument("--password-login", metavar="LOGIN_NAME")
    actions.add_argument("--refresh-session", action="store_true")
    actions.add_argument("--session-status", action="store_true")
    parser.add_argument("--area-code", default="86")
    parser.add_argument("--qr-file", type=Path, default=Path("/tmp/biubiu-login-qr.png"))
    parser.add_argument(
        "--session-file", type=Path, default=Path("/tmp/biubiu-session.json")
    )
    args = parser.parse_args()

    if args.session_status:
        try:
            print(json.dumps(session_status(args.session_file), indent=2))
            return 0
        except Exception as exc:
            print(f"session status failed: {exc}", file=sys.stderr)
            return 1

    if args.refresh_session:
        try:
            refresh_session(args.session_file)
            print(f"SESSION_REFRESHED {args.session_file}")
            return 0
        except Exception as exc:
            print(f"session refresh failed: {exc}", file=sys.stderr)
            return 1

    try:
        device_id = resolve_device_id(args.device_id, args.device_id_file)
    except (ValueError, OSError) as exc:
        print(f"invalid device identity: {exc}", file=sys.stderr)
        return 2
    if args.authorize:
        try:
            return authorize(device_id, args.qr_file, args.session_file)
        except Exception as exc:
            print(f"authorization failed: {exc}", file=sys.stderr)
            return 1

    payload = client_context(device_id)
    endpoint = "capi/qrcodelogin.startQRCodeLogin"
    operation = "probe"
    if args.send_sms:
        payload.update({"mobile": args.send_sms, "areaCode": args.area_code})
        endpoint = "capi/login.sendSmsCode"
        operation = "send SMS"
    elif args.sms_login:
        phone, code = args.sms_login
        payload.update(
            {"mobile": phone, "smsCode": code, "areaCode": args.area_code}
        )
        endpoint = "capi/login.loginWithSmsCode"
        operation = "SMS login"
    elif args.password_login:
        password = getpass.getpass("biubiu password: ")
        payload.update(
            {
                "loginName": args.password_login,
                "password": password,
                "areaCode": args.area_code,
            }
        )
        endpoint = "capi/login.loginByPassword"
        operation = "password login"
    else:
        payload.update({"qrCodeScene": 1, "loginAuthUrl": "pages/Home/index"})

    try:
        result = request(endpoint, payload)
    except urllib.error.HTTPError as exc:
        print(f"HTTP {exc.code}: {exc.read().decode(errors='replace')}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"{operation} failed: {exc}", file=sys.stderr)
        return 1

    if args.sms_login or args.password_login:
        try:
            store_session(
                args.session_file,
                device_id,
                "sms" if args.sms_login else "password",
                result,
            )
        except Exception as exc:
            print(f"{operation} failed: {exc}", file=sys.stderr)
            return 1
        print(f"LOGIN_SUCCESS {args.session_file}")
        return 0
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
