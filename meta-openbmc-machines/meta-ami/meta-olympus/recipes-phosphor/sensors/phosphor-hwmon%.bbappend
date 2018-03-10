FILESEXTRAPATHS_prepend_olympus := "${THISDIR}/${PN}:"

SRC_URI_prepend_olympus = "file://iio-hwmon.conf"

do_install_append() {
        install -d ${D}/etc/default/obmc/hwmon/
        install -m 644 ${WORKDIR}/iio-hwmon.conf ${D}/etc/default/obmc/hwmon/iio-hwmon.conf
}

NAMES = " \
        apb/i2c@1e78a000/i2c-bus@80/tmp423@4c \
        apb/i2c@1e78a000/i2c-bus@1c0/tmp423@4c \
        "

ITEMSFMT = "ahb/{0}.conf"

ITEMS = "${@compose_list(d, 'ITEMSFMT', 'NAMES')}"

ENVS = "obmc/hwmon/{0}"
SYSTEMD_ENVIRONMENT_FILE_${PN} += "${@compose_list(d, 'ENVS', 'ITEMS')}"

