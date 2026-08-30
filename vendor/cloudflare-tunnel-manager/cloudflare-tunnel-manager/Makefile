include $(TOPDIR)/rules.mk

PKG_NAME:=cloudflare-tunnel-manager
PKG_VERSION:=1.0.0
PKG_RELEASE:=2
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/cloudflare-tunnel-manager
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=Web Servers/Proxies
  TITLE:=Multi-account Cloudflare Tunnel manager
  DEPENDS:=+cloudflared +ca-bundle +curl +jsonfilter
  PKGARCH:=all
endef

define Package/cloudflare-tunnel-manager/description
  Multi-account Cloudflare Tunnel, ingress route, DNS, secret, and
  procd instance manager for OpenWrt.
endef

define Build/Compile
endef

define Package/cloudflare-tunnel-manager/conffiles
/etc/config/cloudflare-tunnel-manager
/etc/cloudflare-tunnel-manager/
endef

define Package/cloudflare-tunnel-manager/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/cloudflare-tunnel-manager.config \
		$(1)/etc/config/cloudflare-tunnel-manager

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/cloudflare-tunnel-manager.init \
		$(1)/etc/init.d/cloudflare-tunnel-manager

	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/95-cloudflare-tunnel-manager \
		$(1)/etc/uci-defaults/95-cloudflare-tunnel-manager

	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/cloudflare-tunnel-manager \
		$(1)/usr/libexec/cloudflare-tunnel-manager

	$(INSTALL_DIR) $(1)/etc/cloudflare-tunnel-manager/secrets/accounts
	$(INSTALL_DIR) $(1)/etc/cloudflare-tunnel-manager/secrets/tunnels
endef

define Package/cloudflare-tunnel-manager/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	mkdir -p /etc/cloudflare-tunnel-manager/secrets/accounts \
		/etc/cloudflare-tunnel-manager/secrets/tunnels \
		/tmp/cloudflare-tunnel-manager /var/log/cloudflare-tunnel-manager
	chmod 0700 /etc/cloudflare-tunnel-manager \
		/etc/cloudflare-tunnel-manager/secrets \
		/etc/cloudflare-tunnel-manager/secrets/accounts \
		/etc/cloudflare-tunnel-manager/secrets/tunnels \
		/tmp/cloudflare-tunnel-manager \
		/var/log/cloudflare-tunnel-manager
	chmod 0600 /etc/config/cloudflare-tunnel-manager
	/etc/init.d/cloudflare-tunnel-manager enable >/dev/null 2>&1 || true
fi
exit 0
endef

$(eval $(call BuildPackage,cloudflare-tunnel-manager))
