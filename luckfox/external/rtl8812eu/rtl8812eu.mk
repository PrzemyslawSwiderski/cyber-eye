RTL8812EU_VERSION = v5.15.0.1
RTL8812EU_SITE = https://github.com/svpcom/rtl8812eu/
RTL8812EU_SITE_METHOD = git
RTL8812EU_LICENSE = GPL-2.0

define RTL8812EU_BUILD_CMDS
    sed -i 's/CONFIG_PLATFORM_I386_PC = y/CONFIG_PLATFORM_I386_PC = n/g' $(@D)/Makefile
    sed -i 's/CONFIG_PLATFORM_ARM_RPI = n/CONFIG_PLATFORM_ARM_RPI = y/g' $(@D)/Makefile
    $(MAKE) -C $(LINUX_DIR) M=$(@D) \
        ARCH=$(KERNEL_ARCH) \
        CROSS_COMPILE=$(TARGET_CROSS) \
        modules
endef

define RTL8812EU_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/8812eu.ko \
        $(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/8812eu.ko
endef

$(eval $(generic-package))