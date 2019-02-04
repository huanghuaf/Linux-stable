#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-
if [ ! -f .config ];then
	make vexpress_defconfig
fi

make bzImage -j4 ARCH=arm CROSS_COMPILE=arm-linux-gnueabi-
make dtbs

