include $(TOPDIR)/rules.mk

PKG_NAME:=agent-hub
PKG_VERSION:=1.0.0
PKG_RELEASE:=4
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/agent-hub
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=OpenWrt manager for lightweight AI agent runtimes
  DEPENDS:=+picoclaw +nullclaw +zeroclaw +jshn +uci
  USERID:=agenthub:agenthub
endef

define Package/agent-hub/description
  Generates native configuration for PicoClaw, NullClaw, and ZeroClaw from
  one UCI profile and runs exactly one selected runtime under a non-root user.
endef

define Build/Compile
endef

define Package/agent-hub/conffiles
/etc/config/agent-hub
endef

define Package/agent-hub/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/agent-hub.config $(1)/etc/config/agent-hub

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/agent-hub.init $(1)/etc/init.d/agent-hub

	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/agent-hub-generate $(1)/usr/libexec/agent-hub-generate
	$(INSTALL_BIN) ./files/agent-hub-metrics $(1)/usr/libexec/agent-hub-metrics
	$(INSTALL_BIN) ./files/agent-hub-chat $(1)/usr/libexec/agent-hub-chat
	$(INSTALL_BIN) ./files/agent-hub-chat-worker $(1)/usr/libexec/agent-hub-chat-worker
	$(INSTALL_BIN) ./files/agent-hub-picoclaw-web $(1)/usr/libexec/agent-hub-picoclaw-web

	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) ./files/agent-hubctl $(1)/usr/sbin/agent-hubctl
endef

define Package/agent-hub/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	chmod 0600 /etc/config/agent-hub 2>/dev/null || true
	mkdir -p /etc/agent-hub/managed /etc/agent-hub/native
	chown root:agenthub /etc/agent-hub /etc/agent-hub/managed /etc/agent-hub/native 2>/dev/null || true
	chmod 0710 /etc/agent-hub /etc/agent-hub/managed /etc/agent-hub/native 2>/dev/null || true
	/etc/init.d/agent-hub enable >/dev/null 2>&1 || true
fi
exit 0
endef

$(eval $(call BuildPackage,agent-hub))
