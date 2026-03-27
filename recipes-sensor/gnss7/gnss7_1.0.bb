SUMMARY = "GNSS Sensor Library"
LICENSE = "CLOSED"

SRC_URI = " \
    file://gnss.c \
    file://gnss.h \
"

# 依存ライブラリ
DEPENDS = "curl cjson"

S = "${WORKDIR}"

do_configure[noexec] = "1"

# 共有ライブラリとしてコンパイル
do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -shared -fPIC -o libgnss.so gnss.c -lcurl -lcjson
}

# インストール
do_install() {
    install -d ${D}${libdir}
    install -m 0755 libgnss.so ${D}${libdir}

    install -d ${D}${includedir}
    install -m 0644 gnss.h ${D}${includedir}
}

FILES:${PN} += "${libdir}/libgnss.so"
FILES_SOLIBSDEV = ""
