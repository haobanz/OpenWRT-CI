# biubiu-acc

`biubiu-acc` is a clean-room OpenWrt client built for interoperability with a
user's own biubiu account. It does not contain vendor binaries, private keys,
captured sessions, or code copied from a decompiler.

## Current milestone

The `biubiu-accctl` binary implements the independently verified account
envelope and three user-authorized login methods. Version 0.3.0 also adds an
independent, offline-tested codec for the acceleration API's separate ADAT
envelope:

- one ephemeral 16-byte value used as the account AES key and IV;
- service-specific padding used by the public login API;
- RSA PKCS#1 v1.5 wrapping with the service's public key;
- QR challenge creation, polling, and authorization-code exchange;
- phone SMS code request and SMS code exchange;
- TLS certificate and hostname verification enabled by default.
- independent 16-byte AES key and IV for acceleration requests;
- PKCS#7 AES-128-CBC payload protection and separate RSA wrapping of key/IV;
- RSA key-version handling and an explicit key-rotation response state.

The QR exchange, a user-authorized SMS exchange, and session refresh were
verified against the production service on 2026-09-03. Password endpoint
validation used deliberately invalid credentials; no password was retained.
The reference lab tool persists and refreshes a session atomically without
printing its credentials. The acceleration codec is validated offline with a
generated RSA key; it does not embed the app's protected bootstrap value and
does not contact the acceleration service during tests. Transport acceleration
is not implemented in this milestone, so the package is built as an installable
test artifact and is not installed in the firmware image yet.

## Usage

Generate and retain one device UUID, then use it for the full login flow.
For SMS login:

```sh
biubiu-accctl --device-id "$DEVICE_ID" sms-send 13800000000 86
biubiu-accctl --device-id "$DEVICE_ID" sms-login 13800000000 123456 86
```

For QR login:

```sh
biubiu-accctl --device-id "$DEVICE_ID" qr-start
biubiu-accctl --device-id "$DEVICE_ID" qr-poll "$QR_TOKEN"
biubiu-accctl --device-id "$DEVICE_ID" login-code "$CONNECT_CODE"
```

Commands return JSON on stdout. Session-bearing output must be stored in a
root-only file and must never be included in logs or bug reports. Transport or
JSON parsing failures return exit status 1, invalid CLI input returns 2, and a
service response other than `SUCCESS` returns 3.

Password login prompts on the controlling terminal so the password never
appears in argv or the process list:

```sh
biubiu-accctl --device-id "$DEVICE_ID" password-login 13800000000 86
```

The future LuCI flow will use root-only secret storage instead of UCI.

Run the local cipher check with:

```sh
biubiu-accctl self-test
```

See [docs/protocol.md](docs/protocol.md) for the observed state machine and
the remaining data-plane work.
