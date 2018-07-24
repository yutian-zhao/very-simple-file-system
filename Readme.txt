A Very Very Simple Filesystem
(C) Eric McCreath 2006, 2008, 2010 - GPL
(based on the simplistic RAM filesystem McCreath 2001)

to make use:
    make -C /usr/src/linux-headers-2.6.32-23-generic/  SUBDIRS=$PWD modules
(or just make, with the accompanying Makefile)

to load use:
    sudo insmod vvsfs.ko
(may need to copy vvsfs.ko to a local filesystem first)

to make a suitable filesystem:
    dd of=myvvsfs.raw if=/dev/zero bs=512 count=100
    ./mkfs.vvsfs myvvsfs.raw
(could also use a USB device etc.)

to mount use:
    mkdir testdir
    sudo mount -o loop -t vvsfs myvvsfs.raw testdir

to use a USB device:
    create a suitable partition on USB device (exercise for reader)
        ./mkfs.vvsfs /dev/sdXn
    where sdXn is the device name of the usb drive
        mkdir testdir
        sudo mount -t vvsfs /dev/sdXn testdir

use the file system:
    cd testdir
    echo hello > file1
    cat file1
    cd ..

unmount the filesystem:
    sudo umount testdir

remove the module:
    sudo rmmod vvsfs