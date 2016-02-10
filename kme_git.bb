SUMMARY = "Kernel Memory Editor"
DESCRIPTION = "KME allows semi-realtime symbolic editing of process memory, kernel memory."
HOMEPAGE = "https://github.com/repoocaj/kme"
LICENSE = "GPL-2.0"
LIC_FILES_CHKSUM = "file://COPYING;md5=361b6b837cad26c6900a926b62aada5f"

SRC_URI = "git://github.com/repoocaj/kme.git;branch=autotools_update"
SRCREV = "ff9770a751151de419548f882af617d1fcaaaa9c"

S = "${WORKDIR}/git"

DEPENDS = "ncurses"

inherit autotools

