include $(TOPDIR)/rules.mk

PKG_NAME:=picoclaw
PKG_VERSION:=0.3.1
PKG_RELEASE:=2

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-linux-aarch64.tar.gz
PKG_SOURCE_URL:=https://github.com/sipeed/picoclaw/releases/download/v$(PKG_VERSION)/picoclaw_Linux_arm64.tar.gz?
PKG_HASH:=e78ab0de05d000698ff8b32458e71cb3134e52e9eaf540e3323a72e9914de060
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE
PKG_MAINTAINER:=haobanz

include $(INCLUDE_DIR)/package.mk

define Package/picoclaw
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Lightweight AI agent runtime for constrained devices
  URL:=https://github.com/sipeed/picoclaw
  DEPENDS:=@aarch64 +ca-bundle
endef

define Package/picoclaw/description
  PicoClaw ARM64 static runtime and official WebUI launcher, packaged for OpenWrt.
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	$(HOST_TAR) -xzf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR) \
		picoclaw picoclaw-launcher LICENSE
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/picoclaw/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/picoclaw $(1)/usr/bin/picoclaw
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/picoclaw-launcher $(1)/usr/bin/picoclaw-launcher
endef

$(eval $(call BuildPackage,picoclaw))
