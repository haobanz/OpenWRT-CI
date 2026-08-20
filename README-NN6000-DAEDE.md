# NN6000 v2 DAED profile

This is the self-maintained profile used by the DAEDE build workflow.

- Device: `link_nn6000-v2`
- Target: `qualcommax/ipq60xx`
- Proxy stack: OpenClash, dae, daed, and luci-app-daede
- Kernel support: detached `vmlinux-btf`, `kmod-sched-bpf`, and XDP sockets
- Pinned daed source: see `vendor/daede/REVISION`

The workflow publishes both firmware images and the matching APK packages in
GitHub Releases. The APK packages are tied to the exact target kernel and
must not be mixed with another firmware build.

For local builds, use the ASCII-only maintenance tree and run:

```sh
cd /home/moran/DEV/Custom_OpenClash_Rules/OpenWRT-CI-local
HOST_DEPS_DIR=/tmp/openwrt-host-deps/root JOBS=8 \
  bash Scripts/Build-NN6000-DAEDE.sh
```
