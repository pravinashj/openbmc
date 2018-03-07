SUMMARY = "KCS interface daemon"

SECTION = "examples"

LICENSE = "MIT"

LIC_FILES_CHKSUM = "_file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
inherit obmc-phosphor-systemd
SRC_URI = "\
		file://kcsbridge.c \
		file://obmc-kcs.service \
"

S = "${WORKDIR}"
ROOT = "${STAGING_DIR_TARGET}"
DEPENDS += "dbus \
                  "
SYSTEMD_SERVICE_${PN} = " obmc-kcs.service"
LDFLAGS += "-L ${ROOT}/usr/lib/ -lsystemd -lsdbusplus -ldbus-1 -ldbus-glib-1"
#CFLAGS += ""
LDFLAGS += "-pthread"
do_compile() {  
	${CC} kcsbridge.c -o kcsbridge -I${ROOT}/usr/include/ -I${ROOT}/usr/include/systemd ${LDFLAGS}
}

do_install() {
	install -d ${D}${bindir}
	install -m 0755 kcsbridge ${D}${bindir}
	install -d ${D}${sysconfdir}/systemd/system
	install -m 0644 ${WORKDIR}/obmc-kcs.service ${D}${sysconfdir}/systemd/system

}



















