// test_vram_write.c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

int main(void){
    int fd = open("/dev/vram", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    size_t vsize = 0x4000;
    uint8_t *m = mmap(NULL, vsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    // write "Hello" at (row 10, col 10) with bright white on blue
    int row = 10, col = 10;
    const char *s = "Hello from /dev/vram!";
    int i;
    for (i = 0; s[i]; ++i){
        size_t idx = (row * 80 + col + i) * 2;
        m[idx] = s[i];
        m[idx+1] = 0x1F; // attribute byte (white on blue)
    }

    munmap(m, vsize);
    close(fd);
    return 0;
}
