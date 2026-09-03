# biubiu-acc

`biubiu-acc` is a clean-room OpenWrt client built for interoperability with a
user's own biubiu account. It does not contain vendor binaries, private keys,
captured sessions, or code copied from a decompiler.

## Current milestone

The `biubiu-accctl` binary implements the independently verified account
envelope and three user-authorized login methods. Version 0.7.1 adds a bounded
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
  boundaries with command-specific success checks;
- a disabled-by-default procd supervisor with explicit, truthful preflight
  states and no firewall or route mutations;
- a LuCI page for SMS login, session renewal/removal, whole-LAN or DHCP target
  scope, built-in Steam/CS2/Epic selection, optional profile IDs, public-key
  import, process status, self-test, and logs;
- a read-only match-status view that samples IPv4 conntrack tuples against
  bounded Steam/CS2 destination-port hints and reports packet/byte counters;
- a machine-readable catalog using official public Steam and Epic network
  guidance; identity domains are separated from explicit content/CDN
  exclusions, and broad generic web and Epic port ranges are not automatic
  match hints;
- a mode `0600` one-shot request boundary so the LuCI SMS code is read by the C
  client on stdin instead of appearing in a process argument.

The QR exchange, a user-authorized SMS exchange, and session refresh were
verified against the production service on 2026-09-03. Password endpoint
validation used deliberately invalid credentials; no password was retained.
Both the reference lab and OpenWrt C clients persist and refresh a session
atomically without printing its credentials. The acceleration codec and key
cache and Bolt codec are validated offline with generated or synthetic values;
they do not embed the app's protected bootstrap value and do not contact the
acceleration service during tests. Both `biubiu-acc` and
`luci-app-biubiu-acc` are preinstalled in the NN6000 firmware. Transport
acceleration is still not implemented, so the UI cannot start acceleration and
the package does not modify nftables or route traffic.

## Usage

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
a service response other than `SUCCESS` returns 3.

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
remain inert until the control and data planes are completed.

The acceleration bootstrap is deliberately external. Prepare a root-owned,
mode `0600` file containing the provider-compatible value in the exact form
`VERSION|BASE64_X509_DER`, then import it and inspect only its non-secret
metadata:

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

See [docs/protocol.md](docs/protocol.md) for the observed state machine and
the remaining data-plane work.
