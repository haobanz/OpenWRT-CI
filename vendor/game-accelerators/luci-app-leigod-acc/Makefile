#
# Copyright (C) 2008-2014 The LuCI Team <luci@lists.subsignal.org>
#
# This is free software, licensed under the Apache License, Version 2.0 .
#

include $(TOPDIR)/rules.mk

LUCI_TITLE:=Leigod game accelerator for firewall4 OpenWrt
LUCI_PKGARCH:=all
LUCI_DEPENDS:=+luci-base +luci-compat +leigod-acc +luci-app-upnp
LUCI_URL:=https://github.com/haobanz/OpenWRT-CI
PKG_VERSION:=1.3.0.30
PKG_RELEASE:=3
PKG_PO_VERSION:=$(PKG_VERSION)-r$(PKG_RELEASE)
LUCI_MAINTAINER:=haobanz
PKG_LICENSE:=Apache-2.0

define Package/luci-app-leigod-acc/conffiles
/etc/config/accelerator
endef

define Package/luci-app-leigod-acc/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	chmod 0755 /etc/init.d/acc
	/etc/init.d/acc enable >/dev/null 2>&1 || true
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
fi
exit 0
endef

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
