include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-agent-hub
PKG_VERSION:=1.0.0
PKG_RELEASE:=4
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-agent-hub
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI manager for Agent Hub
  DEPENDS:=+luci-base +agent-hub
  PKGARCH:=all
endef

define Package/luci-app-agent-hub/description
  LuCI interface for configuring, controlling, and chatting with one AI agent runtime.
endef

define Build/Compile
endef

define Package/luci-app-agent-hub/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-agent-hub.json \
		$(1)/usr/share/luci/menu.d/luci-app-agent-hub.json

	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-agent-hub.json \
		$(1)/usr/share/rpcd/acl.d/luci-app-agent-hub.json

	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/agent-hub
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/agent-hub/main.js \
		$(1)/www/luci-static/resources/view/agent-hub/main.js
endef

define Package/luci-app-agent-hub/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
	/etc/init.d/rpcd restart >/dev/null 2>&1 || true
fi
exit 0
endef

define Package/luci-app-agent-hub/postrm
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
fi
exit 0
endef

$(eval $(call BuildPackage,luci-app-agent-hub))
