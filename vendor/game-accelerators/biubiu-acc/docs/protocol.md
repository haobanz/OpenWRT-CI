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
  "v": 1,
  "k": "base64(RSA-PKCS1-v1_5(aes_key))",
  "i": "base64(RSA-PKCS1-v1_5(iv))",
  "d": "base64(AES-128-CBC-PKCS7(payload, aes_key, iv))"
}
```

The AES key and IV are independently generated 16-byte values. Responses have
an outer `c` status and `d` ciphertext. For `c == 2`, the native client still
decrypts `d` with the current request's AES key and IV. The plaintext is a
root-level object containing integer `v` and string `rsaPublicKey`; the client
validates and persists that key, then retries the original request once.

The Windows installer supplies its initial public key locally. The inspected
payload contains `config/config.ini` with `[KEY] ver=1` and a 384-byte
`config/pub_key.pem`. The latter is Base64-wrapped AES-128-CBC/PKCS7 data;
applying the native file-reader routine's fixed local parameters yields a valid
1024-bit X.509 PEM public key. Its DER SHA-256 fingerprint is independently
checked by the offline self-test. The apparent
`/api/ping-bootstrap.config.base.get?ver=1.0.0` string has only static
initialization/destruction references in this build and is not called by the
runtime request path.

The OpenWrt client embeds only that provider public seed material. A missing
cache is initialized automatically; `acc-key-fetch` explicitly restores the
same seed without a network request. Rotated keys are accepted only after the
same version, RSA type/size, and DER validation, then written atomically to a
mode `0600` file. Status output is limited to version, RSA size, and SHA-256
fingerprint. Manual import remains a recovery path. No private key or account
credential is embedded here.

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
`client` is itself the compact JSON string returned by
`MagaManager.getMgClientEx()`. Its top-level fields are the official
`ConnectRequest.Client` fields (`appId`, `deviceId`, `deviceIdType`, and `ex`);
the extension map carries the app identity and device metadata. Before an
authenticated control request, the official Windows client calls
`setAccountInfo({st, biuid})`; its serialized client then contains those two
account fields in `ex`. The clean-room client copies them only from the
validated private account records and cleanses the authenticated client object
after each request. The member-login `localId` is not assumed to be the
provider's biubiu account ID: the latter is obtained through the Windows
client's `getPcUserInfoById` step and kept in a separate mode `0600` file.
The native request transport also sends the serialized client as
`x-biu-client`, a millisecond trace ID as `x-biu-traceid`, and the selected API
contract as `x-biu-ver`.

PC catalog, account and entitlement calls use a separate Windows client
identity instead of reusing the Android identity. The app version and version
code come from the inspected official Windows package manifest. The channel is
`BPC_1`, matching the packaged client's default and native fallback; mobile
endpoints retain their original mobile identity.

Account session material is kept outside UCI and is never passed to the packet
engine. The mobile control path reconstructs its Cookie transport from the
login response's `cookies[].keyName/value` records. It sends the resulting
header only to the fixed `gtm-main.biubiu001.com` host and, when the response
contains a `domains` list, only if that list covers the host. Cookie names and
values are validated, the complete header is capped at 8 KiB, never logged,
and cleansed after each request. The native Windows `NetModule` path does not
add this Cookie header. It carries the authorized service ticket in
`client.ex`, serializes that client into both the payload and `x-biu-client`,
and sends the application headers `Content-Type`, `x-biu-client`,
`x-biu-traceid`, and `x-biu-ver`.

Static call-graph analysis identifies these control endpoints:

```text
/api/ping-server.game.ns.gameListV2?df=adat&ver=1.0.0
/api/ping-server.game.ns.searchGame?df=adat&ver=1.0.0
/api/ping-server.biuvpn.game.checkSpeedup?df=adat&ver=1.0.0
/api/ping-server.biuvpn.game.getSpeedupConfig?df=adat&ver=1.0.1
/api/ping-signal.open.login.loginV2?ver=1.0.0&df=adat
/api/ping-signal.open.auth.getChannelStV2?ver=1.0.0&df=adat
```

Those are the mobile/signal contracts. Native PC business constants are stored
without `df`, for example:

```text
/api/ping-server.config.base.list?ver=1.0.0
/api/ping-server.game.pc.gameList?ver=1.0.1
/api/ping-feed.search.game.pc?ver=1.0.1
/api/ping-server.game.pc.getGameProfile?ver=1.0.0
/api/ping-account.user.base.getPcUserInfoById?ver=1.0.0
/api/ping-server.biuvpn.game.checkSpeedup?ver=1.0.0
/api/ping-server.biuvpn.game.getPCSpeedupConfig?ver=1.0.0
```

The native request constructor appends exact suffix `&df=adat`, including for
signal login and channel-ticket requests, so the wire order is
`?ver=...&df=adat`; it is not the mobile `?df=adat&ver=...` order. Native signal
login sends `Content-Type: application/json;charset=utf-8`, `x-biu-traceid`,
`platform: windows`, `x-biu-client`, and `x-biu-ver`. The 2026-09-05 Wine
capture contains no `x-mg-appkey`. The version header is the desktop
`appVersion`, loaded from the official package's encrypted `version` manifest;
it is not the engine version. The Wine probe uses the package fallback
application version `1.0.0.0`; the independent client was also accepted with
`5.0.2.64`.

Despite their `ping-signal` path names, the native `loginV2` and
`getChannelStV2` requests use the production API origin
`https://sz-maga.biubiu001.com`. The separate
`http://signal-sp.biubiu001.com` origin and its signed HTTPDNS lookup belong to
the later heartbeat/signal transport path; routing either control request to
that host returns HTTP 404.

Game-list requests use a page number, page size, and optional last sort key.
Search adds a keyword. The mobile entitlement request uses `gameId`, `areaId`,
`space`, and a polling flag. The native PC request instead uses `gameId`,
`areaId`, `polling`, and `useMemberSpeedUpExperience`; on its initial call the
JavaScript value for `lastJitterTime` is undefined, so JSON serialization omits
that member. Console speedup configuration adds `platformId` and a package
request marker. Known platform IDs are Android 2, iOS 3, PC 6, Switch 7,
PlayStation 8, Xbox 9, and Steam Deck 10.

### Native PC lighthouse detection

The native route detector reads `proto`, `detectRound`, `lossThresholdMs`,
`port`, `roundSleepMs`, `batchCount`, `batchSleepMs`, `discardHeadRound`,
`lighthouseList`, and `transferLighthouseList` from `scoutPathConfig`. Missing
scalar values use the official defaults below:

```text
proto=UDP                 port=14125
detectRound=10            lossThresholdMs=1000
roundSleepMs=50           batchCount=30
batchSleepMs=30           discardHeadRound=1
```

One UDP socket serves both lighthouse lists. Every request is exactly one
little-endian 32-bit sequence number. The sequence starts at zero and advances
globally across the main list, transfer list, and all rounds. The detector sends
the main list first, sleeps after each full per-list batch, sleeps once between
non-empty main and transfer lists, and sleeps after every round. The ordinary
OpenWrt path does not reproduce the Windows-only netbar preflight branch that
sends the literal six-byte `BIUBIU` marker.

A response of at least four bytes is matched by its first little-endian word;
the native implementation does not require its source address to equal the
target lighthouse. A sample is valid only when its elapsed time is strictly
less than `lossThresholdMs`, and rounds below `discardHeadRound` are excluded.
The receiver remains open for `max(lossThresholdMs, 200) + 15` milliseconds
after sending completes. Results contain only `id`, `avgMs`, `pt90Ms`, `minMs`,
and `maxMs`. Empty result sets use the loss threshold for `avgMs` and `-1` for
the other statistics. The 90th-percentile index is
`clamp(trunc(sampleCount * 0.9) - 1, 0, sampleCount - 1)` after sorting.

The returned speedup model is profile-driven. It contains controller, detect,
filter, inbound, outbound, router, and business profiles. Router entries map
CIDR/domain/port selectors to an outbound ID and type with an explicit
priority. Inbound profiles include DNS, include/exclude routes, TCP/UDP proxy
addresses, and TUN CIDRs. This is the basis for selecting one LAN client while
leaving other devices untouched.

The 2026-09-05 authorized Wine capture established that native Windows
`loginV2` uses the regular ADAT `{v,k,i,d}` envelope. Its decrypted root is
serialized in `client`, `data`, `id` order. `data` contains `signalSt`,
`engineClient`, and the complete profile channel `list`, in that order.
Each list entry contains `dataChannelIp`, numeric `port`, and string `proType`.
The previously identified fixed-AES serializer at `bbengine+0xf7e50` belongs
to `logoutV2`; it must not be used as the initial login contract.
`client` is the compact
native client JSON encoded as a string. The root `id` comes from
`ClientData.id`. The desktop service zero-initializes that field and does not
update it before calling the legacy three-argument `BiuBiu_StartAccelerate`,
so the wire value is an empty string.
The selected `accPodId` is instead serialized in the separate acceleration
configuration. `BiuBiu_StartAccelerateV2` accepts a fourth argument containing
`ClientData` as JSON, parses it, and delegates to the same legacy entry point;
the desktop service resolves V2 but does not call it. Engine-client members are `appId`,
`engineVersion`, `gameId`, `areaId`, `serverId`, `signalSessionId`, `type`, and
`uid`, in that order. The desktop start path writes the native engine version
literal `1.0.0.0` immediately before calling the login serializer. Separately,
`setClientData(appVersion)` writes ClientData field 7 to offsets `0xb88` and
`0xd88`; `BiuBiu_UpdateClientData` copies offset `0xb88` into the engine global
read by the HTTP transport for `x-biu-ver`, proving that header carries the
desktop application version. The actual initial login has `type=4`,
`serverId=0`, and an empty `signalSessionId`. A non-empty session observed
in the logout serializer is the result of the earlier successful login.

The native login response also uses ADAT. Its `data.data` supplies the signal
session, token, XOR setting, and `channelAuthList` with addresses, ports,
protocols, session IDs, secret types, and channel tickets. `expireTime` is a
relative lifetime in seconds (7200 in the observed response). The client stores
the local response receipt time and computes a fixed `expiresAt`; reloading a
runtime must not extend the authorization lifetime. Independent native login
returned `code=200` and `stateCode=2000000` after adopting this contract.
Heartbeat and channel-renewal timing still require runtime verification.
The renewal request resends the
engine client plus each channel's IP, port, protocol, data-channel session ID,
secret type, and channel type; it does not send the old channel ticket.

The Java-to-native login boundary is now independently mapped. The adapter
renames `signalSessionId` to `sessionId`, Base64-decodes `token` into the binary
`bproxyToken`, and converts every channel authorization into a native record.
Native protocol IDs are `ICMP=1`, `TCP=6`, and `UDP=17`. Before accepting a
login, the engine requires a non-empty signal session, enough returned channels
for the requested profile, and, for each channel, a non-zero data-session ID,
resolved address and port, plus a non-empty channel ticket.

When the optional bproxy capability is present, its authorization consists of
the decoded token, the first TCP channel's data-session ID, and the first byte
of the response's `xor` string. That byte remains an opaque marker. It is not
the profile's separate `isEncrypt` setting, and no observed call path justifies
treating it as a cipher or key.

## Observed direct heartbeat contract

The signal heartbeat is a separate encrypted POST, not a call through the main
ADAT API. Static control-flow analysis and an independent synthetic cipher
round trip establish the production endpoint and request boundary:

```text
https://gtm-signal.biubiu001.com/api/open.heartbeat.heartbeatV2
Content-Type: application/json
x-biu-client: <compact engine-client JSON>
```

The inner JSON contains `uid`, `type`, `appId`, `engineVersion`,
`signalSessionId`, `gameId`, `areaId`, and `engineClient`. `uid` is numeric,
while game and area IDs are serialized as strings. `appId` is `biubiu`.
Platform 7 maps to heartbeat type 5; other observed platforms map to type 1.
The engine-client string contains app version name/code, `os=android`, channel,
network name, Android API level, and build code. The same compact string is
sent in the `x-biu-client` header.

The UTF-8 inner object is encrypted with AES-128-CBC and PKCS#7 padding, then
encoded with Android Base64 default formatting. The outer body is:

```json
{"caller":"ping_android","data":"<base64 ciphertext>"}
```

The AES key and IV come from the app's protected static-data provider. They are
not the ADAT session values, have no verified provider-independent retrieval
path, and are not extracted or embedded here. The offline model therefore
requires both as explicit 16-byte inputs and uses synthetic material in tests.

The decrypted response contains top-level `code`, `msg`, and `data`. Success is
exactly code `2000000`; successful data requires integer `state`. An optional
`dataChannelList` carries `proType`, `channelIp`, and `port`, which the client
turns into `protocol://ip:port` endpoints. Runtime cadence and service error
behavior remain unverified. Configuration defaults suggest a 30-second normal
interval and a 120-second maximum, but they are not yet treated as protocol
requirements.

The main-host interceptor asks the mobile security SDK for five values and URL
encodes them into `x-sign`, `x-mini-wua`, `x-umt`, `x-sgext`, and
`x-bx-version`. It runs only when the final request host equals the configured
`mainHost`; the direct signal heartbeat follows a separate path. A dynamic
configuration flag can disable the interceptor, and both SDK initialization
and factor-generation errors cause the official client to continue with the
original unsigned request. This proves that those headers are optional in the
client pipeline, not that the production server will accept their absence for
every endpoint. The clean-room implementation will neither fabricate them nor
bypass a server-side check. An owner-authorized trace must establish whether
the required control endpoints accept the provider-independent unsigned path.

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

## OpenWrt management and data-plane boundary

Version 0.10.0 extends the procd/LuCI management plane with a whole-LAN or
selected-device scope, a machine-readable Steam, Counter-Strike 2/CSGO, and
Epic Games catalog, read-only conntrack observation, and explicit controls for
the provider's game list, entitlement, profile, signal-login, and channel-ticket
endpoints. Raw game/area/platform identifiers remain in advanced settings.
Account credentials, provider responses, channel tickets, and the normalized
runtime are root-owned mode `0600` files; UCI contains only selection and
non-secret operating policy.

The manager materializes `/etc/biubiu-acc/runtime.json` only after a profile and
channel authorization are present. `biubiu-accd` loads that file as root,
connects to the authorized TCP/UDP channel endpoints and performs the observed
Bolt v2 channel bind. Classic profiles then use direct TCP streams and UDP
datagrams; BBNET remains a separate explicit transport. The daemon exposes
transparent listeners on TCP `18080` and UDP `18081` and rejects an absent,
symlinked, foreign-owned, or broadly readable runtime file.

The Windows engine's v2 bind frame was captured in the authorized native
process. UDP uses outer type `0` and a 21-byte header. TCP uses outer type `1`
and initially includes `ept=1`, giving a 26-byte header. Total length is
little-endian and the remaining base-header control fields are zero.
The following payload is always 73 bytes:

```text
byte 0      constant 1
byte 1      GetTickCount-compatible monotonic milliseconds, uint32 LE
byte 5      signalSessionId, 32 raw bytes
next 4      dataChannelSessionId, uint32 LE
next bytes  channelSt, raw bytes
remainder   zero padding
```

The identifier was compared with `signalSessionId` from the same official
process and matched exactly. Logical profile outbound IDs are used for route
selection, not authentication. The response's 13-byte payload starts after
its variable-length outer header: payload byte 0 is `2`, and its little-endian
status at payload offset 9 is `1`. Checking these offsets against the outer
header instead of the payload incorrectly rejects successful bindings.
The response header extension supplies the one-byte value following
`ept_key=`. Direct TCP streams use this channel-negotiated byte for the
repeated-byte XOR transform.

### Classic TCP online validation

The classic per-flow handshake is 22 bytes: outer type `1`, header length `21`,
little-endian total length, destination IPv4 bytes at offset 5, network-order
destination port at offset 9, and the little-endian data-channel session ID at
offset 17. Its payload byte is `0x20`. Success is the two-byte payload
`0x21 0x22` following the response header; neither byte is a cipher key.
Subsequent traffic is a direct TCP stream transformed using the bind response's
EPT byte, without Confluence framing. The production relay completed an
anonymous HTTP request through a provider node and returned HTTP 301 on
2026-09-05. Loopback tests cover target byte order, the transform in both
directions, and preserving replies after a client half-close.

The client declares classic compatibility version `1.0.0.0`, matching the
official Wine fallback used for validation. A controlled version comparison
returned classic UDP 20xx ports for this version and BoltNext UDP 25xx ports
for `5.0.2.64`; the latter requires a different datagram protocol. The actual
classic configuration reports `bbnet not support` and has no BBNET parameters.
Runtime channels now identify their transport explicitly instead of forcing
classic Bolt through the BBNET implementation below.

### Classic UDP online validation

The final classic UDP packet uses outer type `0`, a 21-byte header, a
little-endian total length, and protocol `17` at offset 4. The remote target
IPv4/port are at offsets 5/9, the original LAN client IPv4/port at 11/15, and
the authorized data-channel session ID at 17. Addresses and ports keep network
byte order; the session ID is little-endian. The UDP payload is unmodified.
The zero context argument in the generic callback at `0x10264a10` is not the
final authorized game-send contract. Actual callers supply channel context,
and sending zero produced an explicit invalid-channel error in the live test.
The active sender calls the generic writer at `0x10265f69`; its first endpoint
passes through the remote-address mapper at `0x10265531` before serialization.
Treating that endpoint as the LAN source sends traffic to the wrong address.

Replies use outer type `1`, with the remote source and LAN client destination
in the same two address slots. Reinjection checks protocol, session ID, and
the active flow's reversed endpoints. The production send/parser path returned
two `example.com` DNS answers through a provider UDP node on 2026-09-05.
This validates payload forwarding, not merely a UDP socket or channel bind.

## Verified BBNET, Confluence, and UDP data path

Static analysis of the official Windows engine establishes that the channel is
not a direct long-lived Bolt socket. After the protocol-specific Bolt v2 bind,
the engine creates a BBNET client over UDP. BBNET is a QuickNet derivative. The
clean-room bridge uses the public MIT-licensed QuickNet implementation and a
small documented extension for the observed second handshake:

```text
SYN1 -> ACK1 -> SYN2(feature 0x0000000c LE || raw bbSrvParam) -> ACK2
```

`bbCliParam` is applied as the client's option string after `Connect`, matching
the Windows call order. BBNET's outer packet stores a per-packet mask in byte 0,
XORs bytes 1 onward with `mask ^ global_mask ^ 0x5a`, and carries the QuickNet
command, conversation, and host identifiers. A local UDP fixture verifies the
exact SYN2 length and bytes rather than treating this handshake as ordinary KCP.

The TCP channel selects BBNET application protocol `1` (KCP). It multiplexes
intercepted TCP flows with Confluence messages:

```text
01 01 event link_id_le32
01 01 05    link_id_le32 payload_length_le32 payload
```

Events are `1=connect`, `2=connected`, `3=connect failed`, `4=close`, and
`5=data`. Once a Confluence link reports connected, the client sends one Bolt
v3 Connect request as its first data message and requires the matching successful
response. All later game bytes on that link are passed as raw Confluence payload;
they are not wrapped in Bolt `0x11` data frames.

The UDP channel selects BBNET application protocol `3` (NACK) and shares one
transport across flows. It does not perform one Bolt Associate handshake per
flow. Every datagram uses this verified header:

```text
0       u8    0
1       u8    header length (21)
2       u16   total length, little endian
4       u8    IP protocol (17 for UDP)
5       u8[4] source IPv4, network order
9       u8[2] source port, network order
11      u8[4] target IPv4, network order
15      u8[2] target port, network order
17      u32   dataChannelSessionId, little endian
21      ...   UDP payload
```

The field at offset 17 is the channel's `dataChannelSessionId`; it is not `bip`.
For a provider reply, source and target are reversed relative to the original
LAN datagram. The daemon requires that reversed tuple and the same session ID
before reinjecting a payload. The profile's `bip` is instead an alternate
channel address used by the native spare/dual-path manager. The current OpenWrt
adapter implements initial endpoint fallback to `bip`; it does not claim full
runtime parity with the native dual-path scheduler.

`biubiu-acc-traffic` installs an `inet biubiu_acc` nftables prerouting chain,
mark `0x6d`, and policy route table `107`. The manager combines the bounded
built-in Steam/CS2 destination-port rules with the explicit IPv4 CIDR/port
rules extracted from Bolt routes in the authorized profile. Exact domain
selectors are normalized into `domain|ports` runtime entries, resolved with
the router's DNS resolver, and installed as time-limited nftables IPv4 sets.
The supervisor refreshes those sets in place, so DNS changes do not restart
the channel. Wildcard/suffix selectors and provider table selectors remain
outside this bounded adapter. Rules can be scoped to `br-lan` or one IPv4
source device.
Activation is transactional: a failed route, nftables, or engine step removes
the changes. OpenClash uses an explicit exclusive policy; if its process is
found, the helper stops it before activation and restores it when acceleration
stops. In-place channel renewal uses a reload path that preserves this
ownership marker, so OpenClash is not restored during the data-plane swap. It
never changes OpenClash configuration files.

The supervisor and LuCI read the real engine state. They distinguish
`profile_required`, `channel_required`, `runtime_required`, `ready`,
`accelerating`, and `traffic_error`; no state is reported as accelerated before
the engine process and steering rules are active. `match-status` remains a
diagnostic conntrack view, but it now evaluates the same normalized provider
CIDR/port rules used by nftables and reports whether provider domain rules are
loaded. Domain set membership is maintained by nftables and is not guessed from
generic web traffic; broad generic ports are never added automatically.

## Native profile metadata boundaries

The inspected native channel object stores `bbstrategy` at offset `+0x20` and
passes it through the profile boundary. In the current Windows build, the
common channel-manager initializer does not read that trailing constructor
argument. There is therefore no evidence that `bbstrategy` selects a transport
mode in the ordinary BBNET path, and the OpenWrt adapter keeps it as opaque
profile metadata.

`highThroughput` is a signed 32-bit field on the native outbound object at
offset `+0x68`, with default value `0`. It is not the byte at channel offset
`+0x1d`; that byte remains unknown. Only the exact value `1` gates the observed
`ChannelDowngrade` and spare-manager failure strategy. The native strategy-key
mapping is `main=1`, `direct=2`, `reconnect=3`, and unknown keys fall back to
`2`. No observed branch uses this field as a bandwidth limit, congestion mode,
or generic performance switch.

UOT is a separate optional forwarding/fallback path. Its observed activation
requires both client and service feature flags, a type-2 outbound, a UDP
channel, and a non-zero channel `uotPort`. Those conditions do not alter the
base BBNET TCP or shared NACK UDP wire format. The OpenWrt adapter consequently
uses direct BBNET unless a future implementation reproduces the proprietary
UOT helper from verified behavior.

## Live verification limits

The implemented control and transport path is covered by offline codec,
storage, bind, handshake, and relay tests. Live provider behavior still cannot
be claimed without a channel created for an account authorized by its owner.
The following checks remain required for a production claim:

1. Run the native control sequence with the automatically installed public seed
   and an owner-authorized account, then confirm the provider returns a usable
   profile and channel authorization, including any encrypted `c=2` key
   rotation.
2. Confirm BBNET establishment, a Confluence TCP Connect, and shared NACK UDP
   datagrams, then verify payload round-trips against an authorized game endpoint.
3. Confirm ticket expiry/renewal and heartbeat behavior during a long session.
4. Verify provider route selectors against live traffic. Exact domain selectors
   now use the router DNS/IP-set adapter; wildcard/suffix and provider-table
   selectors still require a provider-specific mapping, and broad Epic or
   generic web ports are intentionally not guessed.

The transport is neither HTTP, SOCKS, nor a standard VPN. The implemented
BBNET, KCP/NACK selection, Confluence framing, and UDP envelope are based on
verified packet builders and state transitions. UOT has now been traced as an
optional, separately gated local forwarding/fallback process: it requires both
client and service flags, a type-2 outbound, a UDP channel, and a non-zero
`uotPort`; ordinary BBNET continues directly when those conditions are false.
The proprietary UOT helper, FEC, and the full native dual-path scheduler remain
outside the claimed runtime boundary and are not required by the base BBNET
path.

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

## Observed standalone Bolt v3 frame boundary

Focused static analysis of the Windows packet engine establishes a shared frame
prefix. Its `ByteArray` writes frame scalar values in little-endian order.
Offsets are from the beginning of a frame:

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

The native engine contains separate TCP handshake and UDP association
primitives. A connect request is completed only by command
`0x23`; an associate request is completed only by command `0x25`. In both
paths, result `0x22` and a non-zero 16-bit connection value are required before
the corresponding ready flag is set. Each timeout callback first checks that
ready flag and becomes a no-op after success. The offline model exposes the
same command-specific acceptance rule so a response from one path cannot
complete the other.

The dedicated `0x11` builder is independently confirmed. It uses an
exactly 11-byte header with no extension count or result byte:

```text
0      u8   protocol version (3)
1      u8   header length (11)
2      u16  total frame length
4      u8   command (0x11)
5      u32  signal session value
9      u16  connection value
11     ...  packet payload
```

The builder allocates `payload length + 11`, writes the frame length, session,
and connection scalar values in little-endian order, and copies the payload
unchanged at offset 11. Endpoint extension values remain network-shaped: four
IPv4 bytes followed by a big-endian port. Its parser
dispatches command `0x11` using the payload pointer and length derived from
those two frame lengths.

Header length separates extension metadata from an optional payload; total
length bounds the complete frame. The offline model validates both lengths and
rejects truncated or surplus extension bytes. It intentionally does not assign
business meaning to opaque extension types, generate channel authorization, or
open a socket.

The same request/data encoder and response parser are implemented in the
OpenWrt C client and covered by `biubiu-accctl self-test`. They remain useful
protocol fixtures, but static call-path analysis supersedes the earlier data
plane assumption: normal TCP uses Bolt Connect only once inside Confluence and
then raw bytes, while normal UDP uses the shared 21-byte BBNET envelope rather
than per-flow Associate and `0x11` frames. The C test uses only synthetic frames
and performs no network request.
