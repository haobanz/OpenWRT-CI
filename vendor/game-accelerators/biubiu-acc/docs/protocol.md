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

The OpenWrt client now applies that rule directly. It keeps a stable device ID,
stores successful account sessions outside UCI, refuses symlinked or broadly
readable state files, exposes only redacted status, and replaces the old session
only after a successful refresh response. Both files are retained by the
sysupgrade keep-list.

The QR state machine is:

```text
create challenge -> waiting -> scanned -> connect code -> account session
                                  \-> cancelled / expired
```

## Observed acceleration control model

The business API serializes a request object with `data`, `id`, and `client`.
`client` is itself a JSON string; its extension map carries the authenticated
session ticket and account identifier. The account session therefore remains
part of the control plane and is not passed to the packet engine or written to
UCI.

Static call-graph analysis identifies these control endpoints:

```text
/api/ping-server.game.ns.gameListV2?df=adat&ver=1.0.0
/api/ping-server.game.ns.searchGame?df=adat&ver=1.0.0
/api/ping-server.biuvpn.game.checkSpeedup?df=adat&ver=1.0.0
/api/ping-server.biuvpn.game.getSpeedupConfig?df=adat&ver=1.0.1
/api/ping-signal.open.login.loginV2?df=adat&ver=1.0.0
/api/ping-signal.open.auth.getChannelStV2?df=adat&ver=1.0.0
```

Game-list requests use a page number, page size, and optional last sort key.
Search adds a keyword. Entitlement checks use `gameId`, `areaId`, `space`, and
a polling flag. Console speedup configuration adds `platformId` and a package
request marker. Known platform IDs are Android 2, iOS 3, PC 6, Switch 7,
PlayStation 8, Xbox 9, and Steam Deck 10.

The returned speedup model is profile-driven. It contains controller, detect,
filter, inbound, outbound, router, and business profiles. Router entries map
CIDR/domain/port selectors to an outbound ID and type with an explicit
priority. Inbound profiles include DNS, include/exclude routes, TCP/UDP proxy
addresses, and TUN CIDRs. This is the basis for selecting one LAN client while
leaving other devices untouched.

Signal login sends an engine client, the selected data-channel IP/port/protocol
list, and a signal ticket. Its response supplies a signal session, token, XOR
setting, and per-channel authorization values including address, port,
protocol, expiry, session ID, secret type, and channel ticket. Heartbeat and
channel-renewal timing still require runtime verification. The renewal request
resends the engine client plus each channel's IP, port, protocol, data-channel
session ID, secret type, and channel type; it does not send the old channel
ticket.

The main API also adds anti-abuse headers generated by the mobile security SDK.
The clean-room implementation will not fabricate or bypass them. An
owner-authorized trace must first establish which headers are mandatory for
router requests and whether the service offers a provider-independent path.

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

1. Add external bootstrap-key import and cached key rotation to the C service.
2. Implement and runtime-verify game list, game profile, entitlement check, and
   node selection.
3. Decode signal login, heartbeat, and channel authorization responses.
4. Implement the smallest compatible data channel, initially TCP and UDP,
   against user-authorized test sessions.
5. Add nftables/TUN steering for one selected LAN IPv4 address.
6. Add procd, ubus, LuCI, traffic counters, conflict checks, and rollback.

The transport layer is not assumed to be ordinary HTTP, SOCKS, or a standard
VPN. Known names such as Bolt, KCP, UOT, and FEC are treated only as clues
until packet formats and state transitions are verified.

## Mobile engine boundary

Static analysis separates the router installer from the packet engine. The
router helper only discovers, installs, and controls the official router
daemon. The mobile packet path is implemented by a separate AArch64 Android
JNI library and exposes initialization, start/stop, mode, rule, network-change,
socket-protection, asset-loading, and TUN callbacks.

The Android service creates the TUN and detaches its file descriptor to the
native engine. The observed default TUN network is `10.222.0.1/16`. Engine
profiles are converted into a typed boundary containing TUN routes, DNS,
domain/IP/port routing rules, host mappings, flow limits, detect tasks, signal
configuration, and TCP/UDP/ICMP Bolt channel descriptors. The native engine
contains its own TCP/UDP stack and signal heartbeat/channel-ticket renewal.

The Android binary is not portable to OpenWrt: it is JNI-bound and dynamically
depends on Android system libraries. It is therefore an interoperability
reference only and is neither vendored nor redistributed. The router-native
implementation requires a clean Linux TUN adapter and an independently
implemented, runtime-verified data channel.

## Observed Bolt v3 frame boundary

Focused static analysis of the packet engine establishes a shared, big-endian
frame prefix. Offsets are from the beginning of a frame:

```text
0      u8   protocol version in the low nibble (3)
1      u8   header length
2      u16  total frame length
4      u8   command
```

The observed connect and associate requests continue with a 32-bit signal
session value and an 8-bit extension count. Their fixed header is therefore 10
bytes. Each extension is encoded as an 8-bit type, an 8-bit length, and exactly
that many value bytes. The request builder emits endpoint-shaped six-byte
values for types 1, 2, and 7, a one-byte value for type 5, and variable opaque
values for types 6, 9, and 10. Endpoint values contain four IPv4 bytes followed
by a big-endian port.

The observed response reader instead consumes a 32-bit session value and a
16-bit connection value. Commands that carry a result then add one result byte
before the extension count. A result value of `0x22` enters the success path
for both connect and associate responses. The verified command values are:

```text
0x11  associated data
0x22  connect request
0x23  connect response
0x24  UDP associate request
0x25  UDP associate response
0x26  close/abort path
0x27  error response
```

Header length separates extension metadata from an optional payload; total
length bounds the complete frame. The offline model validates both lengths and
rejects truncated or surplus extension bytes. It intentionally does not assign
business meaning to opaque extension types, generate channel authorization, or
open a socket. Runtime confirmation against an owner-authorized session remains
required before this codec can become a transport implementation.
