# NN6000 v2 local build

This profile is maintained in the ASCII-only tree:

`/home/moran/DEV/Custom_OpenClash_Rules`

## Pinned sources

- ImmortalWrt fork: `d75b347`
- daede vendor tree: `935028e943e5e0af88fab4791424a774b6e2df74`
- Target: `qualcommax/ipq60xx`, device `link_nn6000-v2`
- Kernel: `6.18.44`

The image includes OpenClash, dae, daed, `kmod-sched-bpf`,
`kmod-xdp-sockets-diag`, and an exact-kernel detached `vmlinux-btf` package.
HomeProxy and sing-box are disabled.

## Build

Run from the CI repository:

```sh
cd /home/moran/DEV/Custom_OpenClash_Rules/OpenWRT-CI-local
HOST_DEPS_DIR=/tmp/openwrt-host-deps/root SKIP_FEEDS=1 JOBS=8 \
  bash Scripts/Build-NN6000-DAEDE.sh
```

The profile-only configuration check is:

```sh
cd /home/moran/DEV/Custom_OpenClash_Rules/OpenWRT-CI-local
HOST_DEPS_DIR=/tmp/openwrt-host-deps/root SKIP_FEEDS=1 DEFCONFIG_ONLY=1 JOBS=8 \
  bash Scripts/Build-NN6000-DAEDE.sh
```

Generated files are placed in:

`/home/moran/DEV/Custom_OpenClash_Rules/immortalwrt-local/bin/targets/qualcommax/ipq60xx`

## Current image

Published release:

`https://github.com/haobanz/OpenWRT-CI/releases/tag/NN6000-v2-20260820`

Use this file for a normal LuCI or SSH upgrade:

`immortalwrt-qualcommax-ipq60xx-link_nn6000-v2-squashfs-sysupgrade.bin`

SHA256:

`4ce96c40e7bfe57edf4908220579d8377b4fd0c2d1b6003bbec86911f7a6b241`

Back up the router before flashing. Use only the `link_nn6000-v2` sysupgrade
image; the factory image is for the device's initial or recovery flashing
workflow. Keep the backup archive under `backups/`.
