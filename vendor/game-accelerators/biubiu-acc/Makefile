include $(TOPDIR)/rules.mk

PKG_NAME:=biubiu-acc
PKG_VERSION:=0.4.0
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
  DEPENDS:=+ca-bundle +libcurl +libjson-c +libopenssl
  URL:=https://www.biubiu001.com/
endef

define Package/biubiu-acc/description
  An independently implemented OpenWrt client for user-authorized biubiu
  sessions. The current milestone implements verified account login and the
  separately observed acceleration ADAT envelope, with root-only persistent
  session refresh; transport is in development.
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
}
exit 0
endef

$(eval $(call BuildPackage,biubiu-acc))
