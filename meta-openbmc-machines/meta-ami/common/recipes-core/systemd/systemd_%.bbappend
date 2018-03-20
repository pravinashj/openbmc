FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
SRC_URI += "file://default.network"

FILES_${PN} += "${libdir}/systemd/network/default.network"


do_install_append() {
        install -m 644 ${WORKDIR}/default.network ${D}${libdir}/systemd/network/

        #TODO Remove after this issue is resolved
        #https://github.com/openbmc/openbmc/issues/152
}
