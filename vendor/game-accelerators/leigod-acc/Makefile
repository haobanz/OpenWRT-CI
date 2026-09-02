include $(TOPDIR)/rules.mk

PKG_NAME:=leigod-acc
PKG_VERSION:=1.3.0.30
PKG_RELEASE:=3
PKG_MAINTAINER:=haobanz
PKG_LICENSE:=Apache-2.0

include $(INCLUDE_DIR)/package.mk

define Package/leigod-acc
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Leigod game accelerator runtime manager
  DEPENDS:=@(x86_64||arm||mipsel||aarch64) \
	+ca-bundle +curl +libpcap +kmod-tun +iptables-nft \
	+kmod-ipt-nat +kmod-ipt-ipset +ipset
  URL:=https://www.leigod.com/
  PKGARCH:=all
endef

define Package/leigod-acc/description
  Downloads and verifies the proprietary Leigod router runtime on first use.
  The vendor binary is not distributed by this package.
endef

define Build/Compile
endef

define Package/leigod-acc/install
	$(INSTALL_DIR) $(1)/usr/sbin/leigod
	$(INSTALL_BIN) ./files/leigod-runtime $(1)/usr/sbin/leigod/leigod-runtime
	$(INSTALL_DATA) ./files/version $(1)/usr/sbin/leigod/version
endef

$(eval $(call BuildPackage,leigod-acc))
