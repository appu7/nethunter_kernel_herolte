#!/bin/bash
# Stock Samsung kernel for Samsung Galaxy S7 build script by jcadduono

################### BEFORE STARTING ################
#
# download a working toolchain and extract it somewhere and configure this
# file to point to the toolchain's root directory.
#
# once you've set up the config section how you like it, you can simply run
# ./build.sh [VARIANT]
#
##################### VARIANTS #####################
#
# xx   = International Exynos
#       SM-G930F
#
# duos = International Exynos Duos
#       SM-N920C
#
###################### CONFIG ######################

# root directory of NetHunter herolte git repo (default is this script's location)
RDIR=$(pwd)

[ $VER ] || \
# version number
VER=$(cat $RDIR/VERSION)

# directory containing cross-compile arm64 toolchain
TOOLCHAIN=$HOME/build/toolchain/android-arm64-4.9

# amount of cpu threads to use in kernel make process
THREADS=5

############## SCARY NO-TOUCHY STUFF ###############

export ARCH=arm64
export CROSS_COMPILE=$TOOLCHAIN/bin/aarch64-linux-android-

[ "$DEVICE" ] || DEVICE=herolte
[ "$TARGET" ] || TARGET=samsung
[ "$1" ] && {
	VARIANT=$1
} || {
	VARIANT=xx
}
DEFCONFIG=${TARGET}_${DEVICE}_defconfig
VARIANT_DEFCONFIG=variant_${VARIANT}_defconfig

[ -f "$RDIR/arch/$ARCH/configs/${DEFCONFIG}" ] || {
	echo "Config $DEFCONFIG not found in $ARCH configs!"
	exit 1
}

[ -f "$RDIR/arch/$ARCH/configs/$VARIANT_DEFCONFIG" ] || {
	echo "Device variant/carrier $VARIANT not found in $ARCH configs!"
	exit 1
}

export LOCALVERSION=$VARIANT-$VER

KDIR=$RDIR/arch/$ARCH/boot

CLEAN_BUILD()
{
	echo "Cleaning build..."
	cd $RDIR
	rm -rf build
}

BUILD_KERNEL()
{
	echo "Creating kernel config..."
	cd $RDIR
	mkdir -p build
	make -C $RDIR $DEFCONFIG \
		VARIANT_DEFCONFIG=$VARIANT_DEFCONFIG
	echo "Starting build for ${TARGET}-${DEVICE}-${LOCALVERSION}..."
	make -C $RDIR -j"$THREADS"
}

CLEAN_BUILD && BUILD_KERNEL && echo "Finished building ${TARGET}-${DEVICE}-${LOCALVERSION}!"
