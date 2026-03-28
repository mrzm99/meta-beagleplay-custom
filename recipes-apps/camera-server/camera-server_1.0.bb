SUMMARY = "Camera Streaming Server"
LICENSE = "CLOSED"

SRC_URI = " \
    file://camera_server.service \
    file://camera_streaming_server.c \
"

DEPENDS = "gnss7 imx708"
RDEPENDS:${PN} = "gnss7 imx708"

inherit systemd

SYSTEMD_SERVICE:${PN} = "camera_server.service"

S = "${WORKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o camera_server camera_streaming_server.c -lgnss -limx708 -lpthread
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 camera_server ${D}${bindir}

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/camera_server.service ${D}${systemd_system_unitdir}
}
