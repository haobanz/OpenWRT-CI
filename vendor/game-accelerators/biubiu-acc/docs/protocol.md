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

Verified endpoints:

```text
capi/qrcodelogin.startQRCodeLogin
capi/qrcodelogin.queryLoginStatus
capi/login.autoLoginByCode
capi/login.sendSmsCode
capi/login.loginWithSmsCode
capi/login.loginByPassword
```

The phone login request fields are `mobile`, `areaCode`, and, for the exchange
step, `smsCode`. Password login uses `loginName`, `areaCode`, and `password`;
the password is protected by the encrypted request envelope. The CLI reads it
without echo from the controlling terminal and never accepts it in argv.

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

1. Persist and refresh the authorized account session without exposing it.
2. Reproduce game list, game profile, entitlement check, and node selection.
3. Decode signal login, heartbeat, and channel authorization responses.
4. Implement the smallest compatible data channel, initially TCP and UDP,
   against user-authorized test sessions.
5. Add nftables/TUN steering for one selected LAN IPv4 address.
6. Add procd, ubus, LuCI, traffic counters, conflict checks, and rollback.

The transport layer is not assumed to be ordinary HTTP, SOCKS, or a standard
VPN. Known names such as Bolt, KCP, UOT, and FEC are treated only as clues
until packet formats and state transitions are verified.
