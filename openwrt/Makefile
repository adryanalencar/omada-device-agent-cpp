include $(TOPDIR)/rules.mk

PKG_NAME:=openomada
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/openomada
  SECTION:=net
  CATEGORY:=Network
  TITLE:=OpenOmada native device agent
  DEPENDS:=+libstdcpp
endef

define Package/openomada/description
  Resource-constrained native OpenOmada device agent for OpenWrt.
endef

define Build/Prepare
	$(CP) ../../* $(PKG_BUILD_DIR)/
endef

define Package/openomada/conffiles
/etc/config/openomada
endef

define Package/openomada/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/openomada-agent-native $(1)/usr/sbin/openomada-agent
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/openomada.init $(1)/etc/init.d/openomada
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/openomada.config $(1)/etc/config/openomada
endef

$(eval $(call BuildPackage,openomada))

