SUMMARY = "I2C GNSS 7 Click Reader Application"
DESCRIPTION = ""
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

RDEPENDS:${PN} += "i2c-tools"

SRC_URI = " \
    file://gnss.c \
    file://Makefile \
"

S = "${WORKDIR}"

EXTRA_OEMAKE = "'CC=${CC}' 'CFLAGS=${CFLAGS}' 'LDFLAGS=${LDFLAGS}'"
do_compile() {
    oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/gnss ${D}${bindir}/
}
