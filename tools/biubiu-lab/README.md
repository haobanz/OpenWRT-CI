# biubiu protocol lab

These tools are for user-authorized interoperability testing on a development
machine. Generated QR images and session files belong in `/tmp`; never commit
them.

`biubiu_login_probe.py` is the reference probe used to validate the account
handshake before implementing it in the OpenWrt C client. It requires Python
3 and `cryptography`. A stable device UUID is retained in a mode `0600` file
so multi-step login requests use one device identity.

Phone SMS login does not require the mobile app:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py --send-sms 13800000000
python3 tools/biubiu-lab/biubiu_login_probe.py \
  --sms-login 13800000000 123456
```

Password login prompts without echoing the password or placing it in argv:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py --password-login 13800000000
```

QR login remains available where the mobile client exposes its scanner:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py --authorize
```

Successful SMS, password, or QR login writes a mode `0600` session file to
`/tmp/biubiu-session.json`. For a durable login, select a private state path
outside the repository and keep its parent directory mode `0700`:

```sh
python3 tools/biubiu-lab/biubiu_login_probe.py \
  --session-file "$HOME/.local/state/biubiu-lab/session.json" \
  --session-status
python3 tools/biubiu-lab/biubiu_login_probe.py \
  --session-file "$HOME/.local/state/biubiu-lab/session.json" \
  --refresh-session
```

`--session-status` reports only booleans, a Cookie count, and the login method.
It never prints the device ID, session ID, refresh token, or Cookie values.
`--refresh-session` reads both credentials from the private file and replaces
it atomically only after the service returns a valid new session. The file
contains live account material and must never be committed or attached to a
bug report.

`biubiu_adat_codec.py` is a network-free reference implementation of the
separate acceleration ADAT envelope. Its tests generate a temporary RSA key,
verify independent key/IV wrapping, AES round trips, key rotation handling, and
invalid input rejection:

`biubiu_control_model.py` is also network-free. It extracts the acceleration
identity from a login record and builds the observed game-list, search,
entitlement, speedup-profile, and signal-login request models. It injects the
service ticket and business user ID only into a copied `client.ex` object and
rejects conflicting account data. It does not send requests or generate the
mobile security SDK's anti-abuse headers. It also validates and redacts signal
login authorization data and builds the observed channel-ticket renewal shape.
The verified native handoff maps channel protocols to `ICMP=1`, `TCP=6`, and
`UDP=17`. It Base64-decodes the API token into an opaque `bproxyToken`, selects
the first TCP channel's data-session ID for the bproxy configuration, and
passes only the first byte of the API's `xor` string as an opaque marker. The
marker is deliberately not treated as a cipher or key because no observed
code path supports that interpretation.

`biubiu_profile_model.py` parses the observed JSON engine-profile boundary. It
normalizes TUN CIDRs and route ports, validates outbound references, and models
the TCP, UDP, and ICMP channel descriptors, including the observed `bip`
alternate path. Signal tickets and opaque
channel parameters are excluded from object representations so test failures
cannot print them. This parser does not implement or contact the data channel.

`biubiu_bolt_model.py` models the independently observed Bolt v2 bind and Bolt
v3 transport boundaries. The v2 encoder reproduces the Windows engine's fixed
73-byte credential payload: constant `1`, monotonic tick count, the authorized
`signalSessionId`, data-channel session ID, channel ticket, and zero padding.
TCP uses outer type `1` and initially requests `ept=1`; UDP uses outer type `0`.
The response type/status is inside the payload after the variable-length
outer header. The model also verifies the optional `ept_key=`
header extension. The v3 model encodes connect/associate request headers and
the fixed 11-byte data header, parses synthetic response and data frames, and
preserves typed length-value extensions and payloads. Frame length, session,
and connection scalars use the native engine's verified little-endian order;
endpoint extensions retain IPv4 network byte order. TCP connect and UDP
associate completion remain command-specific and require a non-zero returned
connection ID. Opaque values and payloads are excluded from representations,
and the module has no networking code. These primitives do not imply that the
normal Windows UDP path sends an Associate request. The validated classic
path sends a 21-byte wrapper directly over UDP; BBNET is a separate transport.

`biubiu_bbnet_handshake_test.cpp` drives the vendored MIT-licensed QuickNet
implementation against a local UDP fixture and verifies the official two-stage
BBNET handshake, including the little-endian feature value `0x0c` and exact
`bbSrvParam` bytes in SYN2. `biubiu_confluence_test.c` checks link control/data
framing. `biubiu_bbnet_transport_test.c` checks Confluence-link and datagram
lifecycle using an in-memory transport, while `biubiu_accd_dataplane_test.c`
checks Bolt bind, classic TCP handshake/XOR/half-close behavior, the UDP
21-byte envelope, classic remote/client field order, `dataChannelSessionId`,
reply flags, and endpoint/session rejection. The alternate BBNET fixtures remain
covered separately. None of these fixtures contacts the provider.

The authorized 2026-09-05 online run completed independent ADAT login and
channel renewal, classic TCP/UDP bindings, an HTTP 301 through the production
TCP relay, and two DNS answers through the production UDP send/parser path.
Captured authentication plaintext was removed after structural comparison.

`biubiu_accctl_scout_test.c` exercises the native PC lighthouse detector
against a local UDP echo fixture. It verifies the official scalar defaults,
percentile rule, one-socket main/transfer schedule, globally increasing
little-endian sequence values, and the exact five-field result shape. The
network-free control model separately verifies that a cold native entitlement
request omits `lastJitterTime` rather than serializing an invented value.

`biubiu_match_model.py` compiles the selected built-in game profiles into a
bounded match plan for the whole LAN or one IPv4 device. Steam/Counter-Strike
uses the official Steamworks port hints and explicitly excludes content/CDN
domains; Epic Games remains provider-profile-only because its official port
list is too broad for safe automatic matching. The model is network-free and
does not install firewall rules or claim that traffic is accelerated. The
OpenWrt package consumes the selected hints through its root-only nftables
TPROXY helper after a provider-authorized runtime is prepared.

`biubiu_heartbeat_model.py` models the direct signal heartbeat without opening
a socket. It builds the observed inner payload, `x-biu-client` header and outer
POST body, reproduces AES-128-CBC/PKCS7 with Android Base64 formatting, and
parses returned state plus refreshed channel endpoints. Cipher material is an
explicit caller-supplied input and is hidden from object representations. The
app's protected heartbeat key and IV are not present in this repository; tests
use synthetic 16-byte values only.

Static interceptor analysis also confirms that the main Android pipeline may
inject `x-sign`, `x-mini-wua`, `x-umt`, `x-sgext`, and `x-bx-version` only for
the configured main host. The official client falls through to the original
unsigned request when that SDK path is disabled or unavailable. No generator
for these provider-owned values is implemented here; server-side acceptance of
the unsigned control endpoints still requires an owner-authorized test.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest discover \
  -s tools/biubiu-lab -p 'test_biubiu_*.py' -v
```

The official native RSA seed and the root-level `c=2` key-rotation parser are
exercised only with public or synthetic material. No private key, account
credential, or captured payload is used by these tests, and the suite performs
no provider request.
