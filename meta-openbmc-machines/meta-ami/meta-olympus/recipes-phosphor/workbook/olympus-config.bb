SUMMARY = "Olympus board wiring"
DESCRIPTION = "Board wiring information for the Olympus system."
PR = "r1"

FILESEXTRAPATHS_prepend := "${THISDIR}/:"

inherit config-in-skeleton

SRC_URI_append += "file://olympus-config-Olympus_py.patch"
PROVIDES_remove = "virtual/obmc-inventory-data"
RPROVIDES_${PN}_remove = "virtual-obmc-inventory-data"

