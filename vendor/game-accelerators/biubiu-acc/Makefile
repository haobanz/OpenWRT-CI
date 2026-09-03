include $(TOPDIR)/rules.mk

PKG_NAME:=biubiu-acc
PKG_VERSION:=0.8.0
PKG_RELEASE:=1
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=MIT

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk

define Package/biubiu-acc
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Experimental clean-room biubiu accelerator client
  DEPENDS:=+ca-bundle +ip-full +jshn +jsonfilter +kmod-nft-tproxy +libcurl +libjson-c +libopenssl +nftables +uci
  URL:=https://www.biubiu001.com/
endef

define Package/biubiu-acc/description
  An independently implemented OpenWrt client for user-authorized biubiu
  sessions. It includes the ADAT control client, profile and channel-ticket
  storage, a Bolt v3 TCP/UDP transparent transport, and transactional traffic
  steering with an explicit OpenClash conflict policy.
endef

define Package/biubiu-acc/conffiles
/etc/config/biubiu-acc
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/biubiu-accctl.c $(PKG_BUILD_DIR)/
	$(CP) ./src/biubiu-accd.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Wformat=2 \
		-o $(PKG_BUILD_DIR)/biubiu-accctl \
		$(PKG_BUILD_DIR)/biubiu-accctl.c \
		$(TARGET_LDFLAGS) -lcurl -ljson-c -lcrypto
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Wformat=2 -pthread \
		-o $(PKG_BUILD_DIR)/biubiu-accd \
		$(PKG_BUILD_DIR)/biubiu-accd.c \
		$(TARGET_LDFLAGS) -ljson-c -lcrypto -pthread
endef

define Package/biubiu-acc/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/biubiu-accctl $(1)/usr/bin/biubiu-accctl
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/biubiu-accd $(1)/usr/bin/biubiu-accd
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/biubiu-acc.config $(1)/etc/config/biubiu-acc
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/biubiu-acc.init $(1)/etc/init.d/biubiu-acc
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/biubiu-acc-manager $(1)/usr/libexec/biubiu-acc-manager
	$(INSTALL_BIN) ./files/biubiu-acc-supervisor $(1)/usr/libexec/biubiu-acc-supervisor
	$(INSTALL_BIN) ./files/biubiu-acc-traffic $(1)/usr/libexec/biubiu-acc-traffic
	$(INSTALL_DIR) $(1)/usr/share/biubiu-acc
	$(INSTALL_DATA) ./files/game-catalog.json \
		$(1)/usr/share/biubiu-acc/game-catalog.json
	$(INSTALL_DIR) $(1)/etc/biubiu-acc
	chmod 0700 $(1)/etc/biubiu-acc
	$(INSTALL_DIR) $(1)/lib/upgrade/keep.d
	$(INSTALL_DATA) ./files/biubiu-acc.upgrade \
		$(1)/lib/upgrade/keep.d/biubiu-acc
endef

define Package/biubiu-acc/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	mkdir -p /etc/biubiu-acc
	chmod 0700 /etc/biubiu-acc
	chmod 0600 /etc/config/biubiu-acc 2>/dev/null || true
	if [ "$$(uci -q get biubiu-acc.main.schema_version)" != 2 ]; then
		uci -q set biubiu-acc.main.scope=lan
		uci -q delete biubiu-acc.main.selected_game 2>/dev/null || true
		uci -q add_list biubiu-acc.main.selected_game=steam
		uci -q add_list biubiu-acc.main.selected_game=counter-strike-2
		uci -q add_list biubiu-acc.main.selected_game=epic-games
		uci -q set biubiu-acc.main.schema_version=2
		uci -q commit biubiu-acc
	fi
	[ -n "$$(uci -q get biubiu-acc.main.openclash_mode)" ] || \
		uci -q set biubiu-acc.main.openclash_mode=exclusive
	[ -n "$$(uci -q get biubiu-acc.main.acceleration_enabled)" ] || \
		uci -q set biubiu-acc.main.acceleration_enabled=0
	uci -q commit biubiu-acc
	/etc/init.d/biubiu-acc enable >/dev/null 2>&1 || true
}
exit 0
endef

$(eval $(call BuildPackage,biubiu-acc))
