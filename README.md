# DAEDE + Agent Hub 自用固件

这是一个独立维护的 ImmortalWrt 自用固件项目，重点维护 DAED/dae、OpenClash
和轻量 AI Agent，不再跟随原 OpenWRT-CI 的通用平台编译逻辑。

## 当前目标

当前只维护 `Link NN6000 v2`：

- Target: `qualcommax/ipq60xx`
- Device: `link_nn6000-v2`
- OpenClash、dae、daed、luci-app-daede
- PicoClaw、NullClaw、ZeroClaw 和统一的 Agent Hub LuCI 管理页
- `kmod-sched-bpf`、`kmod-xdp-sockets-diag`
- 与目标内核匹配的 detached `vmlinux-btf`
- 不包含 HomeProxy 和 sing-box

Release 同时提供设备固件、与当前内核匹配的 DAED/dae APK，以及 Agent Hub
的五个 APK。其他设备只有在确认具备 BPF、XDP、内核 BTF 支持并加入独立
配置后才会增加，不会直接套用原仓库的通用配置。

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
`VIKINGYFY/immortalwrt` 的 `main` 分支。检测到上游提交变化后，调用本仓库
自己的 `Build-NN6000-DAEDE.sh`，构建 NN6000 v2 固件、DAED 依赖包和 Agent
Hub 的五个 APK，并创建一个新的 GitHub Release。

构建失败不会更新提交基线，下一轮会自动重试。也可以在 Actions 中手动
运行该 workflow，手动运行会强制构建一次。

## 最新 Release

[查看最新构建](https://github.com/haobanz/OpenWRT-CI/releases/latest)

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
