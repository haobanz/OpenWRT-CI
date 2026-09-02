module("luci.controller.acc", package.seeall)

function index()
	if not nixio.fs.access("/etc/config/accelerator") then
		return
	end

	entry({ "admin", "services", "acc" },
		alias("admin", "services", "acc", "service"), translate("Leigod Acc"), 50)
	entry({ "admin", "services", "acc", "service" },
		cbi("leigod/service"), translate("Leigod Service"), 30).i18n = "acc"
	entry({ "admin", "services", "acc", "device" },
		cbi("leigod/device"), translate("Leigod Device"), 50).i18n = "acc"
	entry({ "admin", "services", "acc", "app" },
		cbi("leigod/app"), translate("Leigod App"), 60).i18n = "acc"
	entry({ "admin", "services", "acc", "notice" },
		cbi("leigod/notice"), translate("Leigod Notice"), 80).i18n = "acc"
	entry({ "admin", "services", "acc", "status" }, call("get_acc_status")).leaf = true
end

function get_acc_status()
	local uci = require "luci.model.uci".cursor()
	local translate = luci.i18n.translate
	local resp = {
		service = translate("Acc Service Disabled"),
		state = {}
	}

	if luci.sys.call("/etc/init.d/acc running >/dev/null 2>&1") == 0 then
		resp.service = translate("Acc Service Enabled")
	end
	for _, typ in ipairs({ "Phone", "PC", "Game", "Unknown" }) do
		local state = uci:get("accelerator", typ, "state")
		local state_text = translate("Not active")
		if state == "1" then
			state_text = translate("Acc Catalog Started")
		elseif state == "2" then
			state_text = translate("Acc Catalog Stopped")
		elseif state == "3" then
			state_text = translate("Acc Catalog Paused")
		end
		resp.state[translate(typ .. "_Catalog")] = state_text
	end

	luci.http.prepare_content("application/json")
	luci.http.write_json(resp)
end
