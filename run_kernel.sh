sudo qemu-system-arm -M vexpress-a9 -smp 4 -m 1024 -kernel arch/arm/boot/zImage -dtb arch/arm/boot/dts/vexpress-v2p-ca9.dtb -nographic -net nic -net tap,ifname=tap1,script=/etc/qemu-ifup
