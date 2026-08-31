include $(TOPDIR)/rules.mk

PKG_NAME:=openomada
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_LICENSE:=MIT
PKG_MAINTAINER:=OpenOmada

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/openomada
  SECTION:=net
  CATEGORY:=Network
  TITLE:=OpenOmada native device agent
  DEPENDS:=+libstdcpp +libjson-c +libopenssl +opennds +conntrack
endef

define Package/openomada/description
  Resource-constrained native OpenOmada device agent for OpenWrt.
endef

define Build/Prepare
	$(RM) -r $(PKG_BUILD_DIR)
	$(INSTALL_DIR) $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

CMAKE_OPTIONS += \
	-DOPENOMADA_BUILD_TESTS=OFF \
	-DOPENOMADA_ENABLE_TLS=ON \
	-DCMAKE_BUILD_TYPE=MinSizeRel

TARGET_CXXFLAGS += -fno-exceptions -fno-rtti

define Package/openomada/conffiles
/etc/config/openomada
endef

define Package/openomada/install
	$(INSTALL_DIR) $(1)/usr
	$(MAKE) -C $(PKG_BUILD_DIR) DESTDIR="$(1)" install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(LN) openomada-agent-native $(1)/usr/sbin/openomada-agent
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/openomada.init $(1)/etc/init.d/openomada
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/openomada.config $(1)/etc/config/openomada
endef

$(eval $(call BuildPackage,openomada))
