# Copyright (C) 2021 Openwrt.org
#
# This is free software, licensed under the Apache License, Version 2.0 .
#

include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI support for NetEase UU game booster
LUCI_DEPENDS:=+luci-base +luci-compat +kmod-tun +libstdcpp +curl +ca-bundle \
	+ip-full +iptables-nft +kmod-ipt-nat
LUCI_PKGARCH:=all
LUCI_URL:=https://github.com/haobanz/OpenWRT-CI

PKG_NAME:=luci-app-uugamebooster
PKG_VERSION:=1.2.0
PKG_RELEASE:=1
LUCI_MAINTAINER:=haobanz
PKG_LICENSE:=Apache-2.0

define Package/$(PKG_NAME)/conffiles
/etc/config/uuplugin
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	chmod 0755 /etc/init.d/uuplugin
	/etc/init.d/uuplugin enable >/dev/null 2>&1 || true
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
fi
exit 0
endef
include $(TOPDIR)/feeds/luci/luci.mk
# call BuildPackage - OpenWrt buildroot signature
