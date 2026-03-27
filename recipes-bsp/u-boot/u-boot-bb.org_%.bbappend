FILESEXTRAPATHS:prepend := "${THISDIR}/files:"


OVERRIDES:append = "${@bb.utils.contains('BEAGLEPLAY_EXPANSION', 'qspi', 'file://0001-arm-dts-ti-add-w25q128-spi-flash-to-mikrobus.patch', ':qspi-mode', d)}"

SRC_URI:append:qspi-mode = " \
    file://w25q128.cfg \
"
# need for w25q128
do_install:append:qspi-mode() {
    install -d ${D}/mnt/spi_flash

    echo "/dev/mtdblock0 /mnt/spi_flash jffs2 defaults,noatime 0 0" >> ${D}${sysconfdir}/fstab
}


SRC_URI += " \
    file://0001-arm64-dts-ti-fix-for-IMX708-support.patch \
    file://0001-arm64-dts-ti-add-cdns_csi2rx0-node.patch \
    file://0001-arm64-dts-ti-fix-I2C-setting-for-IMX708.patch \
    file://0001-arm64-dts-ti-fix-voltage-settings-for-IMX708.patch \
    file://0001-arm64-dts-ti-fix-read-chip-error-for-IMX708.patch \
    file://0002-arm64-dts-ti-fix-delay-settings-for-IMX708.patch \
    file://0001-arm64-dts-ti-fix-delay-time-for-IMX708.patch \
    file://0001-arm64-dts-ti-fix-gpio-settings-for-IMX708.patch \
    file://0002-arm64-dts-ti-add-dphy-csi-2-settings.patch \
    file://imx708.cfg \
"
