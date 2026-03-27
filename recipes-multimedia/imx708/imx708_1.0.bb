SUMMARY = "IMX708 Camera Library"
LICENSE = "CLOSED"

SRC_URI = " \
    file://imx708.c \
    file://imx708.h \
"

# 依存ライブラリ
DEPENDS = "gstreamer1.0 gstreamer1.0-plugins-base"

# pkg-configを使用可能にする
inherit pkgconfig

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -shared -fPIC -o libimx708.so imx708.c $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0)
}

do_install() {
    install -d ${D}${libdir}
    install -m 0755 libimx708.so ${D}${libdir}

    install -d ${D}${includedir}
    install -m 0644 imx708.h ${D}${includedir}
}

FILES:${PN} += "${libdir}/libimx708.so"
FILES_SOLIBSDEV = ""
