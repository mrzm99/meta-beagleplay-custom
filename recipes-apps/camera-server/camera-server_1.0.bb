SUMMARY = "Camera Streaming Server"
LICENSE = "CLOSED"

SRC_URI = " \
    file://camera_streaming_server.c \
"

DEPENDS = "gnss7 imx708"

RDEPENDS:${PN} = "gnss7 imx708"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o camera_server camera_streaming_server.c -lgnss -limx708 -lpthread
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 camera_server ${D}${bindir}
}
