include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-biubiu-acc
PKG_VERSION:=0.9.0
PKG_RELEASE:=1
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-biubiu-acc
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI manager for the clean-room biubiu client
  DEPENDS:=+luci-base +biubiu-acc
  PKGARCH:=all
endef

define Package/luci-app-biubiu-acc/description
  LuCI interface for account sessions, whole-LAN or device scope, built-in game
  selection, diagnostics, and staged acceleration profile management.
endef

define Build/Compile
endef

define Package/luci-app-biubiu-acc/install
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-biubiu-acc.json \
		$(1)/usr/share/luci/menu.d/luci-app-biubiu-acc.json

	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-biubiu-acc.json \
		$(1)/usr/share/rpcd/acl.d/luci-app-biubiu-acc.json

	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/biubiu-acc
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/biubiu-acc/main.js \
		$(1)/www/luci-static/resources/view/biubiu-acc/main.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/biubiu-acc/main.css \
		$(1)/www/luci-static/resources/view/biubiu-acc/main.css
endef

define Package/luci-app-biubiu-acc/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
	/etc/init.d/rpcd restart >/dev/null 2>&1 || true
fi
exit 0
endef

define Package/luci-app-biubiu-acc/postrm
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-indexcache.* /tmp/luci-modulecache
fi
exit 0
endef

$(eval $(call BuildPackage,luci-app-biubiu-acc))
