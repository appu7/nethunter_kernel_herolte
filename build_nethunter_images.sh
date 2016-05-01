#!/bin/bash
# NetHunter kernel for Samsung Galaxy S7 build script by jcadduono
# This build script is for TouchWiz 6.0 support only
#
###################### CONFIG ######################

# root directory of NetHunter herolte git repo (default is this script's location)
RDIR=$(pwd)

# output directory of Image and dtb.img
OUT_DIR=$HOME/build/kali-nethunter/nethunter-installer/kernels/marshmallow

############## SCARY NO-TOUCHY STUFF ###############

ARCH=arm64
KDIR=$RDIR/build/arch/$ARCH/boot

[ "$DEVICE" ] || DEVICE=herolte

MOVE_IMAGES()
{
	echo "Moving kernel Image and dtb.img to $VARIANT_DIR/..."
	mkdir -p "$VARIANT_DIR"
	rm -f "$VARIANT_DIR/Image" "$VARIANT_DIR/dtb.img"
	mv "$KDIR/Image" "$KDIR/dtb.img" "$VARIANT_DIR/"
}

mkdir -p "$OUT_DIR"

[ "$1" ] && VARIANTS="$*"
[ "$VARIANTS" ] || VARIANTS=$(cat "$RDIR/VARIANTS")

for V in $VARIANTS
do
	if [ "$V" = "xx" ]; then
		VARIANT_DIR=$OUT_DIR/$DEVICE
	else
		VARIANT_DIR=$OUT_DIR/$DEVICE$V
	fi
	DEVICE=$DEVICE TARGET=nethunter VARIANT=$V "$RDIR/build.sh" && MOVE_IMAGES
done

echo "Finished building NetHunter kernels!"
