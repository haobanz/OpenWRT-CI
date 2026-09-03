#!/usr/bin/env python3

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "vendor/game-accelerators/biubiu-acc"
LUCI = ROOT / "vendor/game-accelerators/luci-app-biubiu-acc"


class OpenWrtIntegrationTests(unittest.TestCase):
    def test_firmware_selects_core_and_luci_packages(self) -> None:
        config = (ROOT / "Config/NN6000-DAEDE.txt").read_text()
        self.assertIn("CONFIG_PACKAGE_biubiu-acc=y", config)
        self.assertIn("CONFIG_PACKAGE_luci-app-biubiu-acc=y", config)

        injection = (ROOT / "Scripts/Packages-NN6000-DAEDE.sh").read_text()
        self.assertIn("luci-app-biubiu-acc", injection)

    def test_core_package_installs_management_boundary(self) -> None:
        makefile = (CORE / "Makefile").read_text()
        for path in (
            "biubiu-acc.config",
            "biubiu-acc.init",
            "biubiu-acc-manager",
            "biubiu-acc-supervisor",
        ):
            self.assertIn(path, makefile)
        self.assertIn("/etc/config/biubiu-acc", makefile)

    def test_sms_code_is_not_forwarded_in_process_arguments(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        self.assertIn('printf \'%s\\n\' "$code" | "$CLI" sms-login-stdin', manager)
        self.assertNotIn('sms-login "$phone" "$code"', manager)
        self.assertIn("请求文件权限必须为 0600", manager)

    def test_ui_acl_is_narrow(self) -> None:
        acl = (LUCI / "root/usr/share/rpcd/acl.d/luci-app-biubiu-acc.json").read_text()
        self.assertIn('"/usr/libexec/biubiu-acc-manager request"', acl)
        self.assertNotIn('"/usr/bin/biubiu-accctl *"', acl)
        self.assertNotIn('"/bin/sh *"', acl)

    def test_incomplete_transport_cannot_be_reported_ready(self) -> None:
        manager = (CORE / "files/biubiu-acc-manager").read_text()
        supervisor = (CORE / "files/biubiu-acc-supervisor").read_text()
        ui = (LUCI / "htdocs/luci-static/resources/view/biubiu-acc/main.js").read_text()
        self.assertIn("json_add_boolean data_plane_ready 0", manager)
        self.assertIn("json_add_boolean accelerating 0", manager)
        self.assertIn("transport_incomplete", supervisor)
        self.assertIn("数据通道尚未实现", ui)

    def test_core_self_test_includes_bolt_v3(self) -> None:
        source = (CORE / "src/biubiu-accctl.c").read_text()
        self.assertIn('#define BIUBIU_ACC_VERSION "0.6.0"', source)
        self.assertIn("run_bolt_v3_self_test", source)
        self.assertIn('\\"bolt-v3-frame\\"', source)


if __name__ == "__main__":
    unittest.main()
