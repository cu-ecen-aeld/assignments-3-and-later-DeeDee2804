#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Add your kernel build steps here
    # Deep clean the kernel build tree by removing the .config file with any existing configurations
    echo "mrproper"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    # Configure for our "virt" arm dev board we will simulate in QEMU
    echo "defconfig"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    # Build a kernel image for booting with QEMU
    echo "Build kernel image"
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # Build any kernel modules
    echo "Skip module build"
    # make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    # Build the device tree
    echo "Build the device tree"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    #   Configure busybox
    echo "Configure busybox"
    make distclean
    make defconfig
else
    cd busybox
fi

# Make and install busybox
echo "Make and install busybox"
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
echo "Library dependencies"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "program interpreter"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library"

# Add library dependencies to rootfs
export SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
cd "${OUTDIR}/rootfs/lib/"
cp "${SYSROOT}/lib64/ld-2.33.so" "${OUTDIR}/rootfs/lib"
ln ld-2.33.so ld-linux-aarch64.so.1
cd "${OUTDIR}/rootfs/lib64/"
# cp "/home/deedee2804/Downloads/arm-cross-compiler/aarch64-none-linux-gnu/libc/lib64/libm-2.33.so" "${OUTDIR}/rootfs/lib64"
cp "${SYSROOT}/lib64/libm.so.6" "${OUTDIR}/rootfs/lib64"
# ln libm-2.33.so libm.so.6
cp "${SYSROOT}/lib64/libresolv.so.2" "${OUTDIR}/rootfs/lib64"
# ln libresolv-2.33.so libresolv.so.2
cp "${SYSROOT}/lib64/libc.so.6" "${OUTDIR}/rootfs/lib64"
# ln libc-2.33.so libc.so.6

# Make device nodes
cd "${OUTDIR}/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

# Clean and build the writer utility
echo "Clean and build the writer ultility"
cd "${FINDER_APP_DIR}"
make clean
make CROSS_COMPILE all

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir "${OUTDIR}/rootfs/home/conf"
cp "finder.sh" "autorun-qemu.sh" "finder-test.sh" "writer" "${OUTDIR}/rootfs/home"
cp "conf/username.txt" "conf/assignment.txt" "${OUTDIR}/rootfs/home/conf"

# Chown the root directory
echo "Chown the root directory"
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio

# Create initramfs.cpio.gz
echo "Create initramfs.cpio.gz"
cd "${OUTDIR}"
gzip -f initramfs.cpio