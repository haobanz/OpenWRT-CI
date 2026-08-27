include $(TOPDIR)/rules.mk

PKG_NAME:=zeroclaw
PKG_VERSION:=0.8.4
PKG_RELEASE:=2

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION)-linux-aarch64.tar.gz
PKG_SOURCE_URL:=https://github.com/zeroclaw-labs/zeroclaw/releases/download/v$(PKG_VERSION)/zeroclaw-aarch64-unknown-linux-musl.tar.gz?
PKG_HASH:=bcdc4d7c0bc4fd834c7727e7f6ef7809e49945ed55b5bb6f90d7640ca718d4af
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

PKG_LICENSE:=Apache-2.0 OR MIT
PKG_MAINTAINER:=haobanz

include $(INCLUDE_DIR)/package.mk

define Package/zeroclaw
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Rust AI agent runtime
  URL:=https://github.com/zeroclaw-labs/zeroclaw
  DEPENDS:=@aarch64 +ca-bundle
endef

define Package/zeroclaw/description
  ZeroClaw ARM64 musl static runtime and official Web Dashboard, packaged for OpenWrt.
endef

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	$(HOST_TAR) -xzf $(DL_DIR)/$(PKG_SOURCE) -C $(PKG_BUILD_DIR) zeroclaw web/dist
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/zeroclaw/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/zeroclaw $(1)/usr/bin/zeroclaw
	$(INSTALL_DIR) $(1)/usr/share/zeroclawlabs/web/dist
	$(CP) $(PKG_BUILD_DIR)/web/dist/. $(1)/usr/share/zeroclawlabs/web/dist/
endef

$(eval $(call BuildPackage,zeroclaw))
