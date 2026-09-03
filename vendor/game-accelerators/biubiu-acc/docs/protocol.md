# Protocol notes

This document distinguishes verified behavior from hypotheses. It records
wire-level facts required for interoperability, not implementation details
copied from vendor source code.

## Clean-room boundary

- Use only accounts and memberships authorized by the account owner.
- Do not bypass entitlement, rate-limit, device, or regional checks.
- Do not embed or redistribute vendor binaries, client private keys, or live
  session material.
- Public keys are used only for their intended purpose: encrypting requests to
  the service that owns the corresponding private key.
- A hypothesis is not promoted to implementation until confirmed by a local
  test or a user-authorized capture.

## Verified account handshake

Production origin:

```text
https://member-login.biubiu001.com/
```

The JSON request envelope contains:

```json
{
  "k": "base64(RSA-PKCS1-v1_5(ephemeral_key))",
  "v": 1,
  "d": "base64(AES-128-CBC(payload))",
  "i": "same value as k",
  "requestId": "UUIDv4"
}
```

The 16-byte ASCII ephemeral key is also the CBC IV. Before encryption, the
payload receives one `0x0a` marker followed by 1 to 16 bytes whose value is the
number of repeated bytes. The encrypted response is returned in `d` and uses
the same ephemeral key.

## Observed acceleration ADAT envelope

The acceleration API uses a different ADAT construction from the account
service. Static call-graph and native-routine analysis, followed by an
independent offline round trip, establish this request shape:

```json
{
  "k": "base64(RSA-PKCS1-v1_5(aes_key))",
  "v": 1,
  "d": "base64(AES-128-CBC-PKCS7(payload, aes_key, iv))",
  "i": "base64(RSA-PKCS1-v1_5(iv))"
}
```

The AES key and IV are independently generated 16-byte values. Responses have
an outer `c` status and `d` ciphertext. Status `c == 2` requests an RSA public
key refresh and must not be treated as ciphertext.

The initial RSA value is supplied to the Android SDK through its protected
static-data provider. It is not copied into this repository. Rotated keys are
returned as `version|base64(X.509 RSA public key)` by:

```text
https://gtm-main.biubiu001.com/client/1/config.getSecurityKey
```

The bootstrap request includes `df=adat`, `cver=1.0.0`, and `os=android`.
The clean-room client will require an explicitly supplied bootstrap public key
until a documented provider-independent bootstrap path is verified.

## Verified account endpoints

```text
capi/qrcodelogin.startQRCodeLogin
capi/qrcodelogin.queryLoginStatus
capi/login.autoLoginByCode
capi/login.sendSmsCode
capi/login.loginWithSmsCode
capi/login.loginByPassword
capi/login.autoLogin
```

The phone login request fields are `mobile`, `areaCode`, and, for the exchange
step, `smsCode`. Password login uses `loginName`, `areaCode`, and `password`;
the password is protected by the encrypted request envelope. The CLI reads it
without echo from the controlling terminal and never accepts it in argv.

Session refresh requires both values returned by login: `refreshToken` is sent
as `sessionToken`, while the current `sessionId` remains in
`clientUser.sessionId`. Omitting the latter is rejected by the service. A
successful refresh returns a complete replacement `sessionInfo`; callers must
write it atomically and retain mode `0600`.

The QR state machine is:

```text
create challenge -> waiting -> scanned -> connect code -> account session
                                  \-> cancelled / expired
```

## Observed acceleration control model

The router-oriented task model contains these inputs:

```text
session_id, target_id, area_id, platform_id, client_ip
```

The PC-oriented start model contains:

```text
gameId, areaId, sessionId, accMode, gamePlatform, accPodId,
shutdownSysSleep, enableUot, useMemberSpeedUpExperience,
checkSpeedUpInfo
```

This supports a router-native design: select a LAN IP, obtain an authorized
profile, then intercept only that client's game routes. It does not require a
Windows process scanner on the accelerated machine.

## Remaining milestones

1. Port root-only session persistence and refresh from the reference lab to the
   OpenWrt service layer.
2. Add external bootstrap-key import and cached key rotation to the C service.
3. Reproduce game list, game profile, entitlement check, and node selection.
4. Decode signal login, heartbeat, and channel authorization responses.
5. Implement the smallest compatible data channel, initially TCP and UDP,
   against user-authorized test sessions.
6. Add nftables/TUN steering for one selected LAN IPv4 address.
7. Add procd, ubus, LuCI, traffic counters, conflict checks, and rollback.

The transport layer is not assumed to be ordinary HTTP, SOCKS, or a standard
VPN. Known names such as Bolt, KCP, UOT, and FEC are treated only as clues
until packet formats and state transitions are verified.
