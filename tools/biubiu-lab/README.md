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
the TCP, UDP, and ICMP Bolt channel descriptors. Signal tickets and opaque
channel parameters are excluded from object representations so test failures
cannot print them. This parser does not implement or contact the data channel.

`biubiu_bolt_model.py` models the independently observed Bolt v3 frame
boundary. It encodes connect/associate request headers and the fixed 11-byte
associated-data header, parses synthetic response and data frames, and
preserves typed length-value extensions and payloads. All integers use the
verified network byte order. Opaque values and payloads are excluded from
representations, and the module has no networking code.

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

No vendor bootstrap key, account credential, or captured payload is used by
these tests.
