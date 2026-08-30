include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-cloudflare-tunnel-manager
PKG_VERSION:=1.0.0
PKG_RELEASE:=2
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-cloudflare-tunnel-manager
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI multi-account Cloudflare Tunnel manager
  DEPENDS:=+luci-base +cloudflare-tunnel-manager
  PKGARCH:=all
endef

define Package/luci-app-cloudflare-tunnel-manager/description
  LuCI interface for Cloudflare accounts, remotely managed tunnels,
  LAN origin routes, DNS records, process metrics, and logs.
endef

define Build/Compile
endef

define Package/luci-app-cloudflare-tunnel-manager/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-cloudflare-tunnel-manager.json \
		$(1)/usr/share/luci/menu.d/luci-app-cloudflare-tunnel-manager.json

	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-cloudflare-tunnel-manager.json \
		$(1)/usr/share/rpcd/acl.d/luci-app-cloudflare-tunnel-manager.json

	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/cloudflare-tunnel-manager
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/cloudflare-tunnel-manager/main.js \
		$(1)/www/luci-static/resources/view/cloudflare-tunnel-manager/main.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/cloudflare-tunnel-manager/main.css \
		$(1)/www/luci-static/resources/view/cloudflare-tunnel-manager/main.css
endef

define Package/luci-app-cloudflare-tunnel-manager/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
	/etc/init.d/rpcd restart >/dev/null 2>&1 || true
fi
exit 0
endef

define Package/luci-app-cloudflare-tunnel-manager/postrm
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
fi
exit 0
endef

$(eval $(call BuildPackage,luci-app-cloudflare-tunnel-manager))
