include $(TOPDIR)/rules.mk

PKG_NAME:=nullclaw
PKG_VERSION:=2026.5.29
PKG_RELEASE:=1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-linux-aarch64.bin
PKG_SOURCE_URL:=https://github.com/nullclaw/nullclaw/releases/download/v$(PKG_VERSION)/nullclaw-linux-aarch64.bin?
PKG_HASH:=87d4a19c39dd199f6d5f45b3c80e566d4349d0a291e6cc4ac8f573e2fecfa152
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_LICENSE:=MIT
PKG_MAINTAINER:=haobanz

include $(INCLUDE_DIR)/package.mk

define Package/nullclaw
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Minimal Zig AI agent runtime
  URL:=https://github.com/nullclaw/nullclaw
  DEPENDS:=@aarch64 +ca-bundle
endef

define Package/nullclaw/description
  NullClaw ARM64 static runtime, packaged for OpenWrt.
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	cp $(DL_DIR)/$(PKG_SOURCE) $(PKG_BUILD_DIR)/nullclaw
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/nullclaw/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nullclaw $(1)/usr/bin/nullclaw
endef

$(eval $(call BuildPackage,nullclaw))
