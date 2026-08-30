# Cloudflare Tunnel Manager

这是 NN6000 v2 自用固件内维护的 Cloudflare Tunnel 控制面，分为三个包：

- `cloudflared`：来自 ImmortalWrt packages feed 的官方连接器核心
- `cloudflare-tunnel-manager`：账号密钥、Cloudflare API、UCI 和 procd 多实例后端
- `luci-app-cloudflare-tunnel-manager`：设备、端口、域名、状态和日志界面

## 配置模型

`/etc/config/cloudflare-tunnel-manager` 只保存非敏感元数据：账号名称和 ID、
Tunnel ID、运行参数、设备地址、端口、域名和 DNS Record ID。API Token 与
Tunnel Token 分别存入：

```text
/etc/cloudflare-tunnel-manager/secrets/accounts/<section>.token
/etc/cloudflare-tunnel-manager/secrets/tunnels/<section>.token
```

密钥目录权限为 `0700`，文件权限为 `0600`。调用 Cloudflare API 时，账号 Token
写入一次性 `0600` curl 配置文件，通过 `--config` 引用，不放入命令行参数。
临时文件由退出 trap 清理，LuCI ACL 没有读取密钥目录的权限。

## 远端所有权

管理器新建的 Tunnel 标记为 `managed=1`，可以生成远端 Ingress。导入已有
Tunnel 标记为 `managed=0`，只运行 Token，不修改远端配置。每次修改规则前会
读取 Cloudflare 配置版本；版本与本地记录不一致时拒绝覆盖。

新建 DNS 使用 CNAME `<tunnel-id>.cfargotunnel.com`，并写入注释：

```text
Managed by OpenWRT-CI Cloudflare Tunnel Manager
```

只有内容和注释都匹配的记录才会被自动删除。已有同名 DNS 不会被强制替换。
Ingress 或 DNS 任一步失败时，后端使用修改前快照恢复远端配置和本地 UCI。

## API Token 权限

每个账号使用单独的 Cloudflare API Token，至少需要：

```text
Account / Cloudflare Tunnel / Edit
Zone / Zone / Read
Zone / DNS / Edit
```

账号保存时会验证 Tunnel 列表、Zone 列表和 DNS 读取权限；写权限在首次新建
Tunnel 或规则时由 Cloudflare API 验证。不要使用 Global API Key。

## 后端命令

LuCI 只调用固定入口：

```sh
/usr/libexec/cloudflare-tunnel-manager snapshot
/usr/libexec/cloudflare-tunnel-manager zones <account-section>
/usr/libexec/cloudflare-tunnel-manager remote-tunnels <account-section>
/usr/libexec/cloudflare-tunnel-manager logs <manager|tunnel-section>
/usr/libexec/cloudflare-tunnel-manager request
```

写操作的 JSON 固定写入 `/tmp/cloudflare-tunnel-manager/request.json`，最大 64 KiB，
后端串行加锁、验证全部字段后执行，并在退出时删除请求文件。
