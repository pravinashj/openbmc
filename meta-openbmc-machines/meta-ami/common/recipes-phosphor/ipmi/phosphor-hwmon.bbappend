FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
SRC_URI_append += "file://0001-Return_Success_For_Reading_Failed_sesnor_sysfs.cpp.patch"
SRC_URI_append += "file://0002-Voltage-Scale-Zero-mainloop.cpp.patch"

