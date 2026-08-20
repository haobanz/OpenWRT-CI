# 高质量<免费>交流群

点击链接加入群聊【IPQ技术讨论群】：https://qm.qq.com/q/v7nMhzB4oU
该群为普通交流群。

# 高质量<付费>中转站

点击链接加入群聊【LiBwrt-Ai学习】：https://qm.qq.com/q/HTa7OiWNCU
该群为AI中转站群。

# 本地编译器

https://github.com/VIKINGYFY/OWRT-Tools.git

# 自用修改版插件

https://github.com/VIKINGYFY/packages.git

# OpenWRT-CI

官方版：

https://github.com/immortalwrt/immortalwrt.git

自用版：

https://github.com/VIKINGYFY/immortalwrt.git

# U-BOOT

高通版-沉心：

https://github.com/chenxin527/uboot-qsdk12.5-build.git

高通版-小猪：

https://github.com/1980490718/u-boot-2016.git

联发科-全新版：

https://github.com/VIKINGYFY/UBOOT-CI/releases

联发科-官方版：

https://drive.wrt.moe/uboot/mediatek

# 固件简要说明

固件每天早上5点自动编译。

固件信息里的时间为编译开始的时间，方便核对上游源码提交时间。

MEDIATEK系列、QUALCOMMAX系列、ROCKCHIP系列、X86系列。

# Link NN6000 v2 自用构建

本仓库新增 `NN6000-DAEDE` 手动构建流程，目标为 `Link NN6000 v2`
（`qualcommax/ipq60xx`）。构建基于 `VIKINGYFY/immortalwrt`，内置
OpenClash、dae、daed、BPF/XDP 内核模块，以及匹配当前内核的 detached
`vmlinux-btf` 包；HomeProxy 和 sing-box 未选入。

固件与校验和发布在
[NN6000-v2-20260820 Release](https://github.com/haobanz/OpenWRT-CI/releases/tag/NN6000-v2-20260820)：

- `immortalwrt-qualcommax-ipq60xx-link_nn6000-v2-squashfs-sysupgrade.bin`
  SHA256: `4ce96c40e7bfe57edf4908220579d8377b4fd0c2d1b6003bbec86911f7a6b241`
- `immortalwrt-qualcommax-ipq60xx-link_nn6000-v2-squashfs-factory.bin`
  SHA256: `6acca3ced417e1ffde968d3a5e56f01c8244d7b38e32894868bb0aa378277940`

普通升级请使用 `sysupgrade.bin`；`factory.bin` 仅用于设备初始刷写或恢复流程。
刷机前请先备份路由器配置，确认设备型号为 Link NN6000 v2。

# 目录简要说明

workflows——自定义CI配置

Scripts——自定义脚本

Config——自定义配置

#
[![Stargazers over time](https://starchart.cc/VIKINGYFY/OpenWRT-CI.svg?variant=adaptive)](https://starchart.cc/VIKINGYFY/OpenWRT-CI)
