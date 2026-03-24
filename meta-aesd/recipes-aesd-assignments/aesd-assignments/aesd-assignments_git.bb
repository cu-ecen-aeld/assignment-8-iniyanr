LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SUMMARY = "AESD assignments - aesdsocket"
PV = "1.0+git${SRCPV}"

# Note: Changed to https to match your previous snippet, ensuring it's consistent
SRC_URI = "git://github.com/cu-ecen-aeld/assignments-3-and-later-iniyanr.git;protocol=https;branch=main"

SRCREV = "9a5e11561d9d66c8dcd85bd4b4b3f31b618df359"

# Your source is in the server/ subdirectory of the repo
S = "${WORKDIR}/git/server"

# Identify files that belong to the package
FILES:${PN} += "${bindir}/aesdsocket"
FILES:${PN} += "${sysconfdir}/init.d/aesdsocket"

# Ensure pthreads and realtime libs are linked
TARGET_LDFLAGS:append = " -pthread -lrt"

inherit update-rc.d

INITSCRIPT_PACKAGES = "${PN}"
# This MUST match the filename installed in /etc/init.d/
INITSCRIPT_NAME = "aesdsocket"
INITSCRIPT_PARAMS = "defaults 99"

do_configure() {
    :
}

do_compile() {
    oe_runmake
}

do_install () {
    # 1. Create the destination directories in the image folder (${D})
    install -d ${D}${bindir}
    install -d ${D}${sysconfdir}/init.d

    # 2. Install the binary from the source directory (${S})
    install -m 0755 ${S}/aesdsocket ${D}${bindir}/

    # 3. Install the init script and RENAME it to match INITSCRIPT_NAME
    # We take 'aesdsocket-start-stop.sh' from GitHub and install it as 'aesdsocket'
    install -m 0755 ${S}/aesdsocket-start-stop.sh ${D}${sysconfdir}/init.d/aesdsocket
}
