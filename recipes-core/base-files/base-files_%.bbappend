
do_install:append() {
    install -d ${D}/mnt/spi_flash

    echo "/dev/mtdblock0 /mnt/spi_flash jffs2 defaults,noatime 0 0" >> ${D}${sysconfdir}/fstab
}
