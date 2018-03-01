SUMMARY = "Mt-Olympus Power Control"
DESCRIPTION = "Chassis Power ON/OFF Mt-Olympus Platform"
PR = "r1"

inherit obmc-phosphor-systemd
inherit obmc-phosphor-license

S = "${WORKDIR}"

SRC_URI += "file://poweron.sh"
SRC_URI += "file://poweroff.sh"
SRC_URI += "file://powerreset.sh"
SRC_URI += "file://powercycle.sh"

RDEPENDS_${PN} = "bash"

POWERON_TMPL = "chassis-poweron@.service"
POWERON_INSTFMT = "chassis-poweron@{0}.service"
POWERON_TGTFMT = "obmc-chassis-poweron@{0}.target"
POWERON_FMT = "../${POWERON_TMPL}:${POWERON_TGTFMT}.requires/${POWERON_INSTFMT}"

POWEROFF_TMPL = "chassis-poweroff@.service"
POWEROFF_INSTFMT = "chassis-poweroff@{0}.service"
POWEROFF_TGTFMT = "obmc-chassis-poweroff@{0}.target"
POWEROFF_FMT = "../${POWEROFF_TMPL}:${POWEROFF_TGTFMT}.requires/${POWEROFF_INSTFMT}"

POWERRESET_TMPL = "chassis-powerreset@.service"
POWERRESET_INSTFMT = "chassis-powerreset@{0}.service"
POWERRESET_TGTFMT = "obmc-chassis-powerreset@{0}.target"
POWERRESET_FMT = "../${POWERRESET_TMPL}:${POWERRESET_TGTFMT}.requires/${POWERRESET_INSTFMT}"

POWERCYCLE_TMPL = "chassis-powercycle@.service"
POWERCYCLE_INSTFMT = "chassis-powercycle@{0}.service"
POWERCYCLE_TGTFMT = "obmc-chassis-powercycle@{0}.target"
POWERCYCLE_FMT = "../${POWERCYCLE_TMPL}:${POWERCYCLE_TGTFMT}.requires/${POWERCYCLE_INSTFMT}"

SYSTEMD_SERVICE_${PN} += "${POWERON_TMPL} ${POWEROFF_TMPL} ${POWERRESET_TMPL} ${POWERCYCLE_TMPL}"

SYSTEMD_LINK_${PN} += "${@compose_list(d, 'POWERON_FMT', 'OBMC_CHASSIS_INSTANCES')}"
SYSTEMD_LINK_${PN} += "${@compose_list(d, 'POWEROFF_FMT', 'OBMC_CHASSIS_INSTANCES')}"
SYSTEMD_LINK_${PN} += "${@compose_list(d, 'POWERRESET_FMT', 'OBMC_CHASSIS_INSTANCES')}"
SYSTEMD_LINK_${PN} += "${@compose_list(d, 'POWERCYCLE_FMT', 'OBMC_CHASSIS_INSTANCES')}"


do_install() {
	install -d ${D}${sbindir}
	install -m 0755 ${WORKDIR}/poweron.sh ${D}${sbindir}/poweron.sh
	install -m 0755 ${WORKDIR}/poweroff.sh ${D}${sbindir}/poweroff.sh
	install -m 0755 ${WORKDIR}/powerreset.sh ${D}${sbindir}/powerreset.sh
	install -m 0755 ${WORKDIR}/powercycle.sh ${D}${sbindir}/powercycle.sh
}
