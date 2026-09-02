m = Map("accelerator")
m.title = translate("Leigod Accelerator Config")
m.description = translate("Use the Leigod mobile App to bind this router and select devices and games. This firewall4 build uses TUN mode only. NetEase UU and Leigod cannot run at the same time.")

s = m:section(NamedSection, "base", "system")
s.addremove = false

enable = s:option(Flag, "enabled", translate("Enable"))
enable.rmempty = false
enable.default = 0

mode = s:option(ListValue, "mode", translate("Acceleration mode"))
mode:value("tun", translate("TUN (firewall4 compatible)"))
mode.default = "tun"
mode.rmempty = false
mode.readonly = true

auto_upnp = s:option(Flag, "auto_upnp", translate("Enable UPnP for App discovery"))
auto_upnp.rmempty = false
auto_upnp.default = 1
auto_upnp.description = translate("Required by the Leigod App to discover and bind the router. OpenWrt secure mode remains enabled.")

m:section(SimpleSection).template = "leigod/service"

m.apply_on_parse = true
m.on_after_apply = function()
	luci.sys.call("/etc/init.d/acc restart >/dev/null 2>&1 &")
end

return m
