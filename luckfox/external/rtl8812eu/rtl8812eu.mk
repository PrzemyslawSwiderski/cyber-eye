################################################################################
# rtl8812eu
################################################################################

RTL8812EU_VERSION = v5.15.0.1
RTL8812EU_SITE = https://github.com/svpcom/rtl8812eu
RTL8812EU_SITE_METHOD = git
RTL8812EU_LICENSE = GPL-2.0

# Add dependencies
RTL8812EU_DEPENDENCIES = linux

# Fix build commands
define RTL8812EU_BUILD_CMDS
    # Modify Makefile for ARM platform
    $(SED) 's/CONFIG_PLATFORM_I386_PC = y/CONFIG_PLATFORM_I386_PC = n/g' $(@D)/Makefile
    $(SED) 's/CONFIG_PLATFORM_ARM_RPI = n/CONFIG_PLATFORM_ARM_RPI = y/g' $(@D)/Makefile
    
    # For Luckfox specific settings, you might need additional patches
    $(SED) 's/CONFIG_PLATFORM_ARM_RPI = y/CONFIG_PLATFORM_ARM_RPI = n/g' $(@D)/Makefile
    $(SED) 's/CONFIG_PLATFORM_ARM_RK = n/CONFIG_PLATFORM_ARM_RK = y/g' $(@D)/Makefile
    
    $(MAKE) $(TARGET_CONFIGURE_OPTS) \
        -C $(LINUX_DIR) \
        M=$(@D) \
        ARCH=$(KERNEL_ARCH) \
        CROSS_COMPILE=$(TARGET_CROSS) \
        KSRC=$(LINUX_DIR) \
        modules
endef

define RTL8812EU_INSTALL_TARGET_CMDS
    # Create modules directory
    mkdir -p $(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/
    # Install module
    $(INSTALL) -D -m 0644 $(@D)/8812eu.ko \
        $(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/8812eu.ko
    # Update modules.dep
    $(TARGET_DIR)/sbin/depmod -a -b $(TARGET_DIR) $(LINUX_VERSION_PROBED)
endef

$(eval $(generic-package))
