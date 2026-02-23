FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

OVERRIDES:append = "${@bb.utils.contains('BEAGLEPLAY_EXPANSION', 'qspi', 'file://0001-arm-dts-ti-add-w25q128-spi-flash-to-mikrobus.patch', ':qspi-mode', d)}"

SRC_URI:append:qspi-mode = " \
    file://w25q128.cfg \
    file://001-arm-dts-ti-add-w25q128-spi-flash-to-mikrobus \
"
do_install:append:qspi-mode() {
    install -d ${D}/mnt/spi_flash

    echo "/dev/mtdblock0 /mnt/spi_flash jffs2 defaults,noatime 0 0" >> ${D}${sysconfdir}/fstab
}
