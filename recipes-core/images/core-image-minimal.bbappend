CORE_IMAGE_EXTRA_INSTALL += "${@bb.utils.contains('BEAGLEPLAY_EXPANSION', 'gnss', 'gnss7 i2c-tools', '', d)}"
CORE_IMAGE_EXTRA_INSTALL += "${@bb.utils.contains('BEAGLEPLAY_EXPANSION', 'qspi', 'mtd-utils', '', d)}"
