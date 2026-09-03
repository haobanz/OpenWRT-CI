# DAEDE + Agent Hub + Cloudflare Tunnel + 游戏加速自用固件

这是一个独立维护的 ImmortalWrt 自用固件项目，重点维护 DAED/dae、OpenClash、
轻量 AI Agent、多账号 Cloudflare Tunnel、网易 UU/雷神路由器加速插件，
以及独立开发的 biubiu OpenWrt 客户端，不再跟随原 OpenWRT-CI 的通用平台
编译逻辑。

## 当前目标

当前只维护 `Link NN6000 v2`：

- Target: `qualcommax/ipq60xx`
- Device: `link_nn6000-v2`
- OpenClash、dae、daed、luci-app-daede
- PicoClaw、NullClaw、ZeroClaw 和统一的 Agent Hub LuCI 管理页
- cloudflared 与自维护的多账号 Cloudflare Tunnel LuCI 管理页
- 网易 UU 和雷神游戏加速器 LuCI 管理页
- 实验性的 `biubiu-acc` 独立 APK（二维码、短信登录及会话续期已验证）
- `kmod-sched-bpf`、`kmod-xdp-sockets-diag`
- 与目标内核匹配的 detached `vmlinux-btf`
- 不包含 HomeProxy 和 sing-box

Release 同时提供设备固件、与当前内核匹配的 DAED/dae APK、Agent Hub 的
五个 APK、Cloudflare Tunnel 的三个 APK、两套现有游戏加速器的四个 APK
（含雷神中文语言包），以及实验性的 `biubiu-acc` APK。其他设备只有
在确认具备 BPF、XDP、内核 BTF 支持并加入独立配置后才会增加，不会直接套用
原仓库的通用配置。

## Cloudflare Tunnel

LuCI 的 `服务 -> Cloudflare Tunnel` 提供总览、CF 账号、Tunnel、穿透规则和
日志五个页面。它可以维护多个 Cloudflare 账号与多个独立 cloudflared 实例，
直接从 DHCP 租约选择 LAN 设备，再选择协议和端口绑定到指定域名。规则保存时
同步 Cloudflare 远端 Ingress 与 CNAME；同名 DNS 冲突、外部配置版本变化或 API
中途失败都会停止操作并尽量恢复原配置。

账号 API Token 不写入 UCI，也不会出现在 cloudflared 或 curl 的进程参数中。
它们保存在 `/etc/cloudflare-tunnel-manager/secrets/accounts` 的 `0600` 文件中；
Tunnel Token 独立保存在 `secrets/tunnels`。所需 API Token 权限为：

- Account / Cloudflare Tunnel / Edit
- Zone / Zone / Read
- Zone / DNS / Edit

由管理器新建的 Tunnel 可以维护 Ingress 与 DNS。导入已有 Tunnel 默认只读，
仅由本机运行，防止覆盖 Cloudflare 控制台或 Terraform 管理的规则。DNS 删除也
只作用于带本管理器注释的记录，不会接管已有同名记录。详细设计与维护说明见
[`vendor/cloudflare-tunnel-manager/README.md`](vendor/cloudflare-tunnel-manager/README.md)。

## 游戏加速

固件同时安装网易 UU 和雷神的管理插件，但两者默认都不接管流量，
并且启动脚本禁止它们同时运行。LuCI 入口位于：

- `服务 -> 网易UU游戏加速器`
- `服务 -> Leigod Acc`

UU 核心每次需要时从厂商 HTTPS API 获取，根据 API 返回的 MD5 校验后
放入 `/var/tmp/uu`。雷神在第一次启动时下载 ARM64 核心；厂商仅提供
HTTP 端点，因此自维护包对已审核的文件强制固定 SHA-256，校验失败不会
运行。固件只开放适配 firewall4/nftables 的雷神 TUN 模式，UPnP 使用
`miniupnpd-nftables` 且保留安全模式。

两个厂商核心都不会进入 Git 或固件镜像。启用前需先在 OpenClash/dae 中将
要加速的 LAN 设备设为直连，否则可能被两套透明代理重复处理。实现与安全
边界见
[`vendor/game-accelerators/README.md`](vendor/game-accelerators/README.md)。

`biubiu-acc` 采用 clean-room 方式独立实现，只使用用户本人授权的账号会话，
不分发厂商二进制，也不绕过会员、设备或区域校验。当前里程碑已打通二维码，
并验证了手机短信验证码登录与会话安全续期；隐藏密码登录接口也已实现。加速业务
使用另一套独立 key/IV 的 ADAT 信封，当前已完成离线编解码和密钥轮换状态测试，
但不内置应用保护区中的引导公钥。CI 只构建独立 APK，数据通道、指定 LAN 设备
路由和 LuCI 管理页验证完成前不会预装进固件或接管流量。

## Agent Hub

固件同时安装三个适用于 ARM64 的静态 Agent 运行时：

- PicoClaw `0.3.1`
- NullClaw `2026.5.29`
- ZeroClaw `0.8.4`

LuCI 的 `服务 -> Agent Hub` 提供一套通用设置：运行时、模型提供商、API
地址、API Key、模型名、温度、最大输出 Token、监听范围、端口和 Telegram
机器人。保存应用后，配置适配器会生成所选运行时的原生 JSON/TOML，并重启
所选运行时使渠道和模型配置立即生效；三个运行时中只启动所选的一个。

Telegram 标签页统一管理 Bot Token、允许访问的数字用户 ID、群聊提及规则和
可选 API 代理。适配器会分别转换为 PicoClaw `channel_list`、NullClaw
`channels.telegram.accounts` 和 ZeroClaw `channels`/`peer_groups`。启用时必须
至少配置一个用户 ID，空白名单不会退化为公开机器人。

同一页面带有异步聊天控制台，三种运行时都可以直接对话。顶部状态栏实时显示
PID、CPU、RSS 内存占用和
运行时长，不需要额外的常驻监控进程。PicoClaw 的官方 WebUI Launcher 也包含
在 APK 中，可在 Web UI 标签页启用，默认使用 `18800` 端口并要求首次设置登录密码。
ZeroClaw APK 包含官方 Dashboard；将运行时监听范围设为 LAN 后，可从 Agent
Hub 顶部的“Open Web UI”按钮进入。NullClaw 当前没有随官方 ARM64 单文件
版本发布完整 Dashboard，因此使用 Agent Hub 控制台或其聊天渠道。

服务默认关闭并只监听路由器本机。运行进程默认使用独立的 `agenthub` 非 root
账号；Advanced 中可显式启用 Root 系统权限，让所选运行时和 LuCI 聊天任务执行
完整的路由器诊断与管理命令。Root 模式会把模型和已启用的聊天渠道提升为完整
系统控制入口，只应配合可信模型、私有监听范围和严格的 Telegram 用户白名单使用。
PicoClaw 和 NullClaw 使用工作区限制，ZeroClaw 使用自己的风险配置。
通用配置文件位于 `/etc/agent-hub/managed`。关闭“使用通用设置”后，可以在
`/etc/agent-hub/native/<运行时>` 维护完整的原生配置。

SSH 下也可以切换和检查：

```sh
agent-hubctl versions
agent-hubctl select nullclaw
agent-hubctl restart
agent-hubctl status
```

## 自动构建

[`DAEDE-Build.yml`](.github/workflows/DAEDE-Build.yml) 每 6 小时检查
`VIKINGYFY/immortalwrt` 的 `main` 分支以及 `immortalwrt/packages` 中
`net/cloudflared` 的最近提交。检测到任一上游变化，或本仓库的 Tunnel 管理器、
游戏加速适配层、固件配置和构建脚本发生变化后，会调用自己的
`Build-NN6000-DAEDE.sh`，构建
NN6000 v2 固件与独立 APK，并创建新的 GitHub Release。

构建失败不会更新提交基线，下一轮会自动重试。也可以在 Actions 中手动
运行该 workflow，手动运行会强制构建一次。

## 最新 Release

[查看最新构建](https://github.com/haobanz/OpenWRT-CI/releases/latest)

普通升级使用 `squashfs-sysupgrade.bin`；`factory.bin` 只用于初始刷写或
恢复流程。刷机前请备份路由器配置，并确认设备型号为 Link NN6000 v2。

## 本地编译

源码和构建目录使用纯 ASCII 路径：

`/home/moran/DEV/OpenWRT-CI`

```sh
cd /home/moran/DEV/OpenWRT-CI
HOST_DEPS_DIR=/tmp/openwrt-host-deps/root JOBS=8 \
  bash Scripts/Build-NN6000-DAEDE.sh
```

生成物位于：

`/home/moran/DEV/OpenWRT-CI/immortalwrt-local/bin/targets/qualcommax/ipq60xx`

## 自维护规则

ImmortalWrt 和 cloudflared feed 上游可以自动触发重新编译；daed/dae 使用的
vendored 版本则固定在 `vendor/daede/REVISION`，只有经过验证后才更新。
Tunnel 管理器固定在 `vendor/cloudflare-tunnel-manager`，cloudflared 核心继续
使用 ImmortalWrt packages feed，避免复制和滞后维护上游 Go 客户端。
网易 UU/雷神适配层与 biubiu 独立实现固定在 `vendor/game-accelerators`，
上游提交、雷神核心摘要和 biubiu 已验证里程碑记录在该目录的 `REVISION`；
升级或推进数据通道前需重新审核和实机验证。
