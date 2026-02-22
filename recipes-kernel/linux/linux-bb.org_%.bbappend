FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
    file://w25q128.cfg \
    file://0001-arm64-dts-ti-add-w25q128-spi-flash-to-mikrobus.patch \
"
