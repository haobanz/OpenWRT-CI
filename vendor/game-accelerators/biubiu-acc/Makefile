include $(TOPDIR)/rules.mk

PKG_NAME:=biubiu-acc
PKG_VERSION:=0.7.0
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
  DEPENDS:=+ca-bundle +jshn +jsonfilter +libcurl +libjson-c +libopenssl +uci
  URL:=https://www.biubiu001.com/
endef

define Package/biubiu-acc/description
  An independently implemented OpenWrt client for user-authorized biubiu
  sessions. The current milestone implements verified account login, private
  session state, a management supervisor, and the independently observed ADAT
  and Bolt v3 codecs. The traffic transport remains in development.
endef

define Package/biubiu-acc/conffiles
/etc/config/biubiu-acc
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/biubiu-accctl.c $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) \
		-std=c11 -Wall -Wextra -Wformat=2 \
		-o $(PKG_BUILD_DIR)/biubiu-accctl \
		$(PKG_BUILD_DIR)/biubiu-accctl.c \
		$(TARGET_LDFLAGS) -lcurl -ljson-c -lcrypto
endef

define Package/biubiu-acc/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/biubiu-accctl $(1)/usr/bin/biubiu-accctl
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/biubiu-acc.config $(1)/etc/config/biubiu-acc
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/biubiu-acc.init $(1)/etc/init.d/biubiu-acc
	$(INSTALL_DIR) $(1)/usr/libexec
	$(INSTALL_BIN) ./files/biubiu-acc-manager $(1)/usr/libexec/biubiu-acc-manager
	$(INSTALL_BIN) ./files/biubiu-acc-supervisor $(1)/usr/libexec/biubiu-acc-supervisor
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
	/etc/init.d/biubiu-acc enable >/dev/null 2>&1 || true
}
exit 0
endef

$(eval $(call BuildPackage,biubiu-acc))
