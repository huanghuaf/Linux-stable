if [ "x$1" = "xarm" ]; then
	sudo qemu-system-arm -M vexpress-a9 -smp 4 -m 1024 -kernel arch/arm/boot/zImage -dtb arch/arm/boot/dts/vexpress-v2p-ca9.dtb -nographic -net nic -net tap,ifname=tap1,script=/etc/qemu-ifup
elif [ "x$1" = "xarm64" ]; then
	sudo qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 2048 -kernel arch/arm64/boot/Image --append "rdinit=/sbin/init console=ttyAMA0" -nographic -net nic -net tap,ifname=tap1,script=/etc/qemu-ifup
else
	echo "Not support arch $1"
fi
