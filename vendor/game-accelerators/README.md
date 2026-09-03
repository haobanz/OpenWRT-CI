# Game accelerator integrations

This directory contains self-maintained OpenWrt integrations for NetEase UU,
Leigod, and the clean-room biubiu client. UU and Leigod are installed but
disabled by default, and their init scripts prevent them from running at the
same time.

## NetEase UU

- LuCI entry: `Services -> UU Game Booster`
- The AArch64 vendor core is fetched from the vendor HTTPS API when the service
  starts and is verified against the MD5 supplied by that API.
- Runtime files live in `/var/tmp/uu`; no proprietary executable is committed
  to this repository or included in the firmware image.
- The wrapper creates only its own named firewall zone and forwarding, then
  removes those entries when stopped.

## Leigod

- LuCI entry: `Services -> Leigod Accelerator`
- Only TUN mode is exposed on firewall4/nftables builds. The old gateway mode
  depended on firewall3-era scripts and is deliberately unavailable.
- The vendor executable is downloaded on first start. Its endpoint only offers
  HTTP, so every supported executable is pinned to a reviewed SHA-256.
- `miniupnpd-nftables` provides UPnP. Automatic activation is optional and
  keeps secure mode enabled.

## biubiu (experimental)

- `biubiu-acc` is an independent implementation; it contains no vendor binary,
  private key, or embedded account credential.
- Version 0.7.0 includes the completed QR, phone SMS, hidden password, and session-refresh
  account paths in the OpenWrt C client. Device identity and account sessions
  use private persistent files, successful login output is redacted, and refresh
  replaces a session atomically. It also includes an offline-tested codec for
  the acceleration API's separate key/IV ADAT envelope. An external
  `version|base64(X.509 DER)` public key can be validated and cached in a
  root-only file without embedding the app's protected bootstrap value.
- The CLI and LuCI management page are preinstalled and also published as
  standalone APKs. The built-in catalog exposes Steam, Counter-Strike 2/CSGO,
  and Epic Games without requiring raw IDs, with either whole-LAN or one-device
  scope. The disabled-by-default supervisor reports account, scope, game, key,
  and process state but cannot claim acceleration. Settings are staged without
  changing nftables or routes.
- The C self-test now includes the independently verified Bolt v3 request,
  response, and 11-byte data frame boundary. Control API calls, live channels,
  and transport steering remain incomplete.
- Protocol facts, open questions, and the clean-room boundary are documented
  in `biubiu-acc/docs/protocol.md`.

The wrappers warn when OpenClash, Mihomo, dae, or daed is running because game
traffic may otherwise be intercepted twice. Configure the proxy to bypass the
accelerated client before enabling either service.

Upstream revisions, the reviewed Leigod digest, and biubiu implementation
milestones are recorded in `REVISION`.
