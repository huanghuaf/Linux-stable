#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-
#make vexpress_defconfig
make bzImage -j4 ARCH=arm CROSS_COMPILE=arm-linux-gnueabi-
make dtbs

