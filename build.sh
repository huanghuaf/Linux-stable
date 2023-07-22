#!/bin/bash

NR_CPUS=`cat /proc/cpuinfo| grep "processor"| wc -l`

if [ ! -f .config ]; then
	if [ "x$1" = "xarm" ]; then
		make ARCH=$1 vexpress_defconfig
	elif [ "x$1" = "ximx" ]; then
		make ARCH=arm imx6ul_defconfig
	elif [ "x$1" = "xarm64" ]; then
		make ARCH=arm64 defconfig
	else
		echo "Wrong arguments"
		exit 1
	fi
else
	if [ "x$1" = "xarm" ]; then
		if [ "x" = "x$(cat .config | grep CONFIG_ARM=y)" ]; then
			make distclean;
			make ARCH=$1 vexpress_defconfig
		fi
	elif [ "x$1" = "ximx" ]; then
		if [ "x" = "x$(cat .config | grep CONFIG_ARM=y)" ]; then
			make distclean;
			make ARCH=arm imx6ul_defconfig
		fi
	elif [ "x$1" = "xarm64" ]; then
		if [ "x" = "x$(cat .config | grep CONFIG_ARM64=y)" ]; then
			make distclean;
			make ARCH=$1 defconfig
		fi
	else
		echo "Not support ARCH $1"
	fi
fi

if [ "x$1" = "xarm" -o "x$1" = "ximx" ]; then
	if [ "x$1" = "xarm" ];then
		make -j$NR_CPUS ARCH=$1 CROSS_COMPILE=arm-linux-gnueabi- bzImage dtbs
	else
		make -j$NR_CPUS ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- bzImage dtbs
	fi
elif [ "x$1" = "xarm64" ]; then
	make -j$NR_CPUS ARCH=$1 CROSS_COMPILE=aarch64-linux-gnu- Image dtbs
else
	echo "Wrong arguments"
	exit 1
fi
