local mp, s ,o
require("luci.util")

mp = Map("uuplugin")
mp.title = translate("网易UU游戏加速器")
mp.description = translate("启用后使用 UU 主机加速 App 绑定本路由器，并在 App 中选择需要加速的设备和游戏。UU 与雷神不能同时运行；OpenClash 或 DAED 同时运行时应为游戏设备配置绕过规则。")

mp:section(SimpleSection).template  = "uuplugin/uuplugin_status"

s = mp:section(TypedSection, "uuplugin")
s.anonymous = true
s.addremove = false

o = s:option(Flag, "enabled", translate("启用"))
o.default = 0
o.rmempty = false

o = s:option(Value, "model", translate("设备型号"),
        translate("本机设备型号，在APP里方便区分不同路由器，绑定后修改型号需要解绑后重新绑定才生效"))
o.placeholder = "NN6000-v2"

o = s:option(Flag, "addfw", translate("添加 TUN 防火墙区域"),
		translate("默认无需开启。只有 App 已绑定但流量没有进入 UU 时再尝试；关闭服务会删除本插件创建的规则。"))

mp:section(SimpleSection).template  = "uuplugin/uuplugin_qcode"

mp.apply_on_parse = true
mp.on_after_apply = function(self,map)
	luci.sys.call("/etc/init.d/uuplugin restart >/dev/null 2>&1 &")
end

return mp
