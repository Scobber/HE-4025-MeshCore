MESHCORE_HE4025_PACKAGE_DIR:=$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(wildcard $(MESHCORE_HE4025_PACKAGE_DIR)/src),)
  MESHCORE_HE4025_SRC?=$(abspath $(MESHCORE_HE4025_PACKAGE_DIR)/..)
else
  MESHCORE_HE4025_SRC?=$(abspath $(MESHCORE_HE4025_PACKAGE_DIR))
endif

include $(TOPDIR)/rules.mk

PKG_NAME:=meshcore-he4025
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

-include $(INCLUDE_DIR)/uclibc++.mk
include $(INCLUDE_DIR)/package.mk

MESHCORE_HE4025_CXX_DEPENDS:=$(if $(CXX_DEPENDS),$(CXX_DEPENDS),+libstdcpp)

define Package/meshcore-he4025
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=HE-4025 SX1276 MeshCore userspace daemon
  DEPENDS:=+kmod-spi-dev $(MESHCORE_HE4025_CXX_DEPENDS)
endef

define Package/meshcore-he4025/description
HE-4025 SX1276 userspace daemon using spidev, sysfs GPIO, embedded stats UI,
and token-gated sysupgrade OTA.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(MESHCORE_HE4025_SRC)/src $(MESHCORE_HE4025_SRC)/tools \
	$(MESHCORE_HE4025_SRC)/boards $(MESHCORE_HE4025_SRC)/openwrt \
	$(MESHCORE_HE4025_SRC)/daemon.mk $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		-f daemon.mk \
		$(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CFLAGS) -Wall -Wextra" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -std=c++11 -Wall -Wextra" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/meshcore-he4025/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/meshcore-he4025 $(1)/usr/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/sx1276-regversion $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/usr/share/meshcore-he4025/boards
	$(INSTALL_CONF) $(MESHCORE_HE4025_SRC)/boards/dragino-ibb-v1.0.conf \
		$(1)/usr/share/meshcore-he4025/boards/dragino-ibb-v1.0.conf
	$(INSTALL_CONF) $(MESHCORE_HE4025_SRC)/boards/dragino-ibb-v1.0-dual-lora.conf \
		$(1)/usr/share/meshcore-he4025/boards/dragino-ibb-v1.0-dual-lora.conf
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) $(MESHCORE_HE4025_SRC)/openwrt/files/etc/config/meshcore \
		$(1)/etc/config/meshcore
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) $(MESHCORE_HE4025_SRC)/openwrt/files/etc/init.d/meshcore \
		$(1)/etc/init.d/meshcore
endef

$(eval $(call BuildPackage,meshcore-he4025))
