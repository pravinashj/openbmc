#
# This file was derived from the 'Hello World!' example recipe in the
# Yocto Project Development Manual.
#

SUMMARY = "AMI Redfish Server"
SECTION = "redfish"
LICENSE = "Proprietary"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1da0b1af5a77f8f404b2df6d7e4ee37c"

SRCREV = "005737aee8994de7dc01339410473e530b88d6f7"
SRC_URI = "git://git@ubuntu-cloud.us.megatrends.com/redfish/redfish-lua.git;protocol=ssh"

S = "${WORKDIR}/git"

DEPENDS += "turbolua redis luaposix33 bit32 lua-filesystem"

APP_DIR = "${S}/app"

do_compile() {
		cd ${APP_DIR}
		for f in $(find . -name "*.lua"); do
			mkdir -p ${WORKDIR}/output/`dirname "$f"`
			luajit -b -s "$f" ${WORKDIR}/output/"$f"
		done
		cd ${WORKDIR}
}

do_install() {
		install -d ${D}/usr/local/redfish/
		install -d ${D}/usr/local/redfish/oem
		install -d ${D}/usr/local/redfish/extensions/constants
		install -d ${D}/etc/init.d/
		cp -R ${WORKDIR}/output/* ${D}/usr/local/redfish/
		install -m 0755 ${S}/redfish-server ${D}/etc/init.d/redfish-server
		cp -R ${S}/db_init ${D}/usr/local/redfish/db_init
}

FILES_${PN} += "${prefix} ${sysconfdir}"