# Game accelerator integrations

This directory contains self-maintained OpenWrt wrappers for NetEase UU and
Leigod game accelerators. Both services are installed but disabled by default,
and the init scripts prevent them from running at the same time.

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

The wrappers warn when OpenClash, Mihomo, dae, or daed is running because game
traffic may otherwise be intercepted twice. Configure the proxy to bypass the
accelerated client before enabling either service.

Upstream revisions and the reviewed Leigod digest are recorded in `REVISION`.
