# Vendored QuickNet

This directory contains a pinned copy of
[`skywind3000/QuickNet`](https://github.com/skywind3000/QuickNet) at commit:

```
cd2667b4f105cc832e358f0f6821204d244583f5
```

QuickNet is distributed under the MIT license. See `LICENSE` in this
directory.

Local compatibility changes are intentionally limited to behavior observed
in the official biubiu Windows client:

- initialize the session feature word to `0x0000000c`;
- retain the channel `bbSrvParam` value on the client;
- append that value after the four-byte feature word in the SYN2 payload;
- provide the missing legacy `system/option.h` selector used by
  `ProtocolBasic.cpp` (the selector is stored but is not consumed by this
  source revision).

The package builds only the transport implementation needed by the client.
The upstream sample API, validation utility, and obsolete timer sources are
kept for provenance but are not linked into `biubiu-accd`.
