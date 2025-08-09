make
sudo insmod ./vram_mmap.ko
# optionally override parameters:
# sudo insmod ./vram_mmap.ko phys_addr=0xa0000 vsize=0x20000
ls -l /dev/vram
