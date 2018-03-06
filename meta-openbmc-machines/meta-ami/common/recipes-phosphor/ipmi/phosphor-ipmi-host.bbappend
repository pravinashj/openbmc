FILESEXTRAPATHS_prepend := "${THISDIR}/${PN}:"
SRC_URI_append += "file://0001-chassishandler.cpp.patch"
SRC_URI_append += "file://0002-MasterWriteRead_Implementation.patch"

