# biubiu-acc

`biubiu-acc` is a clean-room OpenWrt client built for interoperability with a
user's own biubiu account. It does not contain vendor binaries, private keys,
captured sessions, or code copied from a decompiler.

## Current milestone

The `biubiu-accctl` binary implements the independently verified account
envelope and three user-authorized login methods. Version 0.10.0 includes a bounded
built-in game catalog, whole-LAN or selected-device scope, and read-only
conntrack matching diagnostics to the OpenWrt management plane. The verified
Bolt v3 frame boundary remains covered by the C client's offline self-test. The
current implementation includes:

- one ephemeral 16-byte value used as the account AES key and IV;
- service-specific padding used by the public login API;
- RSA PKCS#1 v1.5 wrapping with the service's public key;
- QR challenge creation, polling, and authorization-code exchange;
- phone SMS code request and SMS code exchange;
- TLS certificate and hostname verification enabled by default.
- independent 16-byte AES key and IV for acceleration requests;
- PKCS#7 AES-128-CBC payload protection and separate RSA wrapping of key/IV;
- RSA key-version handling and an explicit key-rotation response state.
- persistent device identity and mode `0600` account session files;
- atomic session replacement, redacted status output, and session refresh;
- rejection of symlinked, foreign-owned, or group/world-accessible state files.
- external acceleration RSA public-key validation, private caching, and
  redacted version/fingerprint status.
- exact Bolt v3 connect/associate request, response, and 11-byte data-frame
  codecs retained as observed protocol primitives, with command-specific
  success checks;
- a disabled-by-default procd supervisor with explicit, truthful preflight
  states;
- a LuCI page for SMS login, session renewal/removal, whole-LAN or DHCP target
  scope, built-in Steam/CS2/Epic selection, optional profile IDs, public-key
  import, process status, self-test, and logs;
- a read-only match-status view that samples IPv4 conntrack tuples against
  bounded Steam/CS2 hints and normalized CIDR/port rules from the authorized
  provider profile, and reports packet/byte counters;
- a machine-readable catalog using official public Steam and Epic network
  guidance; identity domains are separated from explicit content/CDN
  exclusions, and broad generic web and Epic port ranges are not automatic
  match hints;
- a mode `0600` one-shot request boundary so the LuCI SMS code is read by the C
  client on stdin instead of appearing in a process argument;
- live ADAT control operations for game catalog, search, entitlement, profile,
  signal authorization, and channel-ticket renewal, including the mobile
  Cookie transport restricted to the fixed control host and the session's
  returned domain scope, plus the Windows client's authenticated `client.ex`
  account context and native header contract;
- root-private runtime materialization for authorized TCP and UDP channels,
  including renewal replacement, the observed `bip` alternate endpoint,
  expiry metadata, and normalized provider route rules;
- classic Bolt TCP/UDP channel binding, direct TCP relay with channel EPT
  transformation and half-close handling, and direct UDP forwarding with
  session/endpoint checks;
- a native BBNET transport based on the public MIT-licensed QuickNet protocol:
  KCP plus Confluence links for TCP and NACK plus the native 21-byte datagram
  envelope for UDP;
- the public version-1 seed key shipped by the official Windows installer,
  automatic ADAT `c=2` key rotation from root-level `v`/`rsaPublicKey`, strict
  RSA validation, atomic persistence, and one bounded request retry;
- nftables TPROXY steering for the whole LAN or one selected IPv4 device, with
  a transactional OpenClash exclusive-mode stop/restore path.

The QR exchange, a user-authorized SMS exchange, and session refresh were
verified against the production service on 2026-09-03. Password endpoint
validation used deliberately invalid credentials; no password was retained.
Both the reference lab and OpenWrt C clients persist and refresh a session
atomically without printing its credentials. The acceleration codec, native
seed and rotation path, key cache, and Bolt codec are validated offline with
generated or synthetic values. The package contains only provider public RSA
material, never a private key, and its tests do not contact the acceleration
service.
Both `biubiu-acc` and
`luci-app-biubiu-acc` are preinstalled in the NN6000 firmware. The core
package can start the native TCP/UDP data path only after the account, ADAT
key, provider profile, channel authorization, and runtime file are all
present. It refuses to start when any prerequisite is missing. Static analysis
of the official Windows engine and deterministic packet fixtures cover the
transport layering and framing. A live owner-authorized provider channel is
still required before claiming production acceleration.

On 2026-09-05, the independent C client completed production ADAT signal login,
channel renewal, and classic TCP/UDP bindings. The production TCP relay returned
HTTP 301 from a public target through the provider node; the production UDP
send/parser path returned two DNS answers with matching session and endpoints.
The client declares the observed classic compatibility version `1.0.0.0`;
declaring `5.0.2.64` selects a different BoltNext UDP protocol. The router's
actual Steam/CS2 TPROXY workflow still needs an on-device acceptance run.

## Usage

In LuCI, open `Acceleration configuration -> Select official game`, search the
provider catalog, then select an area and an optional acceleration mode. Saving
validates the game/area/platform/mode tuple against the provider map before
invalidating any old channel state. Names are taken from that map, not trusted
from the browser. A failed lookup leaves the running configuration unchanged.
Automatic mode follows the provider's default; the provider's process mode does
not give a router process-level visibility into LAN devices. Node assignment is
currently automatic; manual node/entry selection is not implemented.

The separate built-in Steam/CS2/Epic checkboxes select LAN traffic hints, not the
provider's acceleration game. An empty initial provider catalog is valid; search
can still return games. Search, area and mode responses expose only public
allowlisted metadata, never raw account or profile responses.

The package retains one device UUID in `/etc/biubiu-acc/device-id` and stores a
successful login in `/etc/biubiu-acc/session.json`. Both files are mode `0600`;
their directory is mode `0700`, and the firmware upgrade keep-list preserves
them. For SMS login:

```sh
biubiu-accctl sms-send 13800000000 86
biubiu-accctl sms-login 13800000000 123456 86
read -r BIUBIU_SMS_CODE
printf '%s\n' "$BIUBIU_SMS_CODE" | biubiu-accctl sms-login-stdin 13800000000 86
unset BIUBIU_SMS_CODE
```

For QR login:

```sh
biubiu-accctl qr-start
biubiu-accctl qr-poll "$QR_TOKEN"
biubiu-accctl login-code "$CONNECT_CODE"
```

Login commands store session-bearing output directly and return only a redacted
success summary. Never include the state files in logs or bug reports. Transport
or JSON parsing failures return exit status 1, invalid CLI input returns 2, and
a provider business failure returns 3. Control responses prefer the nested
`state.code` (`200`, `2000000`, and `2000001` are accepted) or an explicit
`data.success`; a top-level `code=200` alone is only a transport result and does
not override a nested failure.

Password login prompts on the controlling terminal so the password never
appears in argv or the process list:

```sh
biubiu-accctl password-login 13800000000 86
```

Inspect or renew the local login without exposing its values:

```sh
biubiu-accctl session-status
biubiu-accctl session-refresh
biubiu-accctl session-clear
```

`--device-id-file` and `--session-file` can override the two default absolute
paths for testing. The LuCI flow never places account credentials in UCI.
Its entry is `Services -> biubiu accelerator`; the management supervisor is
off by default and only evaluates local preflight state.

Fresh installs default to whole-LAN scope with the Steam, Counter-Strike 2,
and Epic Games catalog entries selected. Selecting a DHCP device switches the
scope to that device. Upgrades from version 0.6.0 migrate once to this layout;
later user choices, including an empty selection, are preserved. These values
remain inert until a profile and channel authorization have been obtained
through the service controls.

The official public seed is installed automatically on the first ADAT request.
It can also be restored explicitly with:

```sh
biubiu-accctl acc-key-fetch
```

This is a local operation and does not contact a bootstrap endpoint. The
official installer ships key version 1 in `config/config.ini` and an obfuscated
`config/pub_key.pem`; static analysis and an independent decrypt/parse check
confirm the public key embedded here. When a normal ADAT response returns
`c=2`, the response `d` is decrypted with that request's AES key and IV. Only a
root-level integer `v` plus valid Base64 X.509 DER `rsaPublicKey` is accepted;
the new key is cached atomically and the original request is retried once.
Malformed rotations fail closed. If manual recovery is needed, prepare a
root-owned, mode `0600` file containing `VERSION|BASE64_X509_DER`, then import
it and inspect only its non-secret metadata:

```sh
biubiu-accctl acc-key-import /tmp/security-key.txt
biubiu-accctl acc-key-status
```

The validated cache is `/etc/biubiu-acc/acceleration-key.json` by default and
is retained across sysupgrade. `--acc-key-file` selects a different absolute
cache path for testing. Import does not initiate an acceleration request.

Run the local cipher check with:

```sh
biubiu-accctl self-test
```

After logging in, the control-plane sequence is:

```sh
biubiu-accctl pc-game-list
biubiu-accctl pc-game-search CS2
biubiu-accctl pc-game-catalog
biubiu-accctl pc-game-map GAME_ID
biubiu-accctl pc-game-options GAME_ID
biubiu-accctl pc-game-selection GAME_ID AREA_ID PLATFORM_ID [ACC_MODE]
biubiu-accctl pc-user-sync
biubiu-accctl pc-context-start GAME_ID AREA_ID PLATFORM_ID [ACC_MODE]
biubiu-accctl pc-check-speedup GAME_ID AREA_ID [POLLING LAST_JITTER_TIME]
biubiu-accctl pc-profile-fetch GAME_ID AREA_ID
biubiu-accctl pc-signal-login GAME_ID AREA_ID PLATFORM_ID
biubiu-accctl pc-runtime-prepare
```

`game-list` is the provider's basic catalog. `pc-game-list` uses the full game
catalog endpoint and stores it separately in `/etc/biubiu-acc/pc-game-list.json`;
the two response models must not be mixed. `pc-game-search` sends the provider's
platform ID `6` and exact paging model. `service-config-fetch` refreshes the
provider's dynamic service configuration and stores it privately.
After member login, `pc-user-sync` privately stores the provider's distinct
biubiu account ID. It
does not treat the member service's `localId` as that ID.
`pc-check-speedup` checks entitlement and stores its
response separately in `/etc/biubiu-acc/pc-entitlement.json`; it deliberately
does not send the console-only `space` field used by `check-speedup`. Its native
Windows request sends `gameId`, `areaId`, `polling`, and
`useMemberSpeedUpExperience`; a cold start omits `lastJitterTime` entirely.
`pc-profile-fetch` consumes the returned scout configuration, performs the
official UDP lighthouse schedule, and sends the measured main and transfer
results to the native PC speedup-profile endpoint.

`pc-channel-renew` repeats the channel authorization step when the provider ticket
is near expiry. The supervisor renews before expiry, rebuilds the runtime from
the new ticket, and reloads nftables/biubiu-accd without temporarily restoring
OpenClash. A failed renewal keeps acceleration enabled for a bounded retry and
never intentionally leaves an expired channel active. The LuCI page exposes the same sequence
	and acceleration start/stop actions. Starting acceleration combines the
	built-in bounded Steam/CS2 destination-port hints with explicit IPv4 CIDR/port
	rules from the authorized profile. Exact provider domain selectors are
	normalized, resolved through the router's DNS resolver, and installed as
	time-limited nftables IPv4 sets; the supervisor refreshes those sets without
	restarting active channels. Wildcard/suffix selectors and provider table
	selectors remain outside this bounded adapter rather than being expanded into
	broad Internet rules.

The traffic engine is root-only and listens on transparent ports `18080` (TCP)
and `18081` (UDP). It uses policy table `107` and mark `0x6d`, and removes both
on stop or startup failure. When OpenClash is running, the default exclusive
policy stops it before installing the rules and restores it after acceleration
stops.

For each authorized channel the daemon first performs the exact Bolt v2 bind.
Classic runtime channels use direct TCP/UDP transport. TCP obtains its EPT byte
from the bind response, sends the native per-target handshake, and relays the
transformed byte stream. UDP sends the remote/client tuple plus channel session
ID and verifies matching return traffic. An explicitly selected BBNET transport
instead opens BBNET with `bbSrvParam` in the second handshake and applies
`bbCliParam` as transport options. TCP uses application protocol 1 (KCP): one
Confluence link is opened per intercepted TCP flow, one Bolt v3 Connect request
is sent through that link, and all bytes after its successful response are raw
stream payload. UDP uses application protocol 3 (NACK): all flows for a channel
share one BBNET session and carry the original source, destination, protocol,
`dataChannelSessionId`, and payload in the verified 21-byte envelope. Provider
replies reverse the source and destination tuple before local reinjection.

See [docs/protocol.md](docs/protocol.md) for the observed state machine,
transport boundary, and live-verification limits.
