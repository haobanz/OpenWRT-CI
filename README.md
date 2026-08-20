# DAEDE 自用固件

这是一个独立维护的 ImmortalWrt DAED/dae 固件项目，不再跟随原
OpenWRT-CI 的通用平台编译逻辑。

## 当前目标

当前只维护 `Link NN6000 v2`：

- Target: `qualcommax/ipq60xx`
- Device: `link_nn6000-v2`
- OpenClash、dae、daed、luci-app-daede
- `kmod-sched-bpf`、`kmod-xdp-sockets-diag`
- 与目标内核匹配的 detached `vmlinux-btf`
- 不包含 HomeProxy 和 sing-box

Release 同时提供设备固件和与当前内核匹配的 DAED/dae APK 包。其他设备
只有在确认具备 BPF、XDP、内核 BTF 支持并加入独立配置后才会增加，不会
直接套用原仓库的通用配置。

## 自动构建

[`DAEDE-Build.yml`](.github/workflows/DAEDE-Build.yml) 每 6 小时检查
`VIKINGYFY/immortalwrt` 的 `main` 分支。检测到上游提交变化后，调用本仓库
自己的 `Build-NN6000-DAEDE.sh`，构建 NN6000 v2 固件和 DAED 依赖包，并创建
一个新的 GitHub Release。

构建失败不会更新提交基线，下一轮会自动重试。也可以在 Actions 中手动
运行该 workflow，手动运行会强制构建一次。

## 最新 Release

[NN6000-v2-20260820](https://github.com/haobanz/OpenWRT-CI/releases/tag/NN6000-v2-20260820)

普通升级使用 `squashfs-sysupgrade.bin`；`factory.bin` 只用于初始刷写或
恢复流程。刷机前请备份路由器配置，并确认设备型号为 Link NN6000 v2。

## 本地编译

源码和构建目录使用纯 ASCII 路径：

`/home/moran/DEV/Custom_OpenClash_Rules`

```sh
cd /home/moran/DEV/Custom_OpenClash_Rules/OpenWRT-CI-local
HOST_DEPS_DIR=/tmp/openwrt-host-deps/root JOBS=8 \
  bash Scripts/Build-NN6000-DAEDE.sh
```

生成物位于：

`immortalwrt-local/bin/targets/qualcommax/ipq60xx`

## 自维护规则

ImmortalWrt 上游可以自动触发重新编译；daed/dae 使用的 vendored 版本则
固定在 `vendor/daede/REVISION`，只有经过验证后才更新。这样可以避免上游
包变化直接破坏当前 NN6000 v2 的 BTF 和内核模块匹配关系。
