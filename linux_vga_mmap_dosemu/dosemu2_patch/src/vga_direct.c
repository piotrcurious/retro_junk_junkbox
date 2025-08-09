// vga_direct.c
// Minimal helper for dosemu2 to use /dev/vram for text output.
//
// Usage: call vga_direct_init() at dosemu startup and vga_direct_putc(row,col,ch,attr)
// or vga_direct_write() where appropriate. If /dev/vram isn't available, vga_direct_init()
// will return 0 and dosemu2 should fall back to normal rendering.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

static int vram_fd = -1;
static uint8_t *vram_map = NULL;
static size_t vram_size = 0x4000; // default 16KiB
static off_t vram_phys = 0xb8000;

int vga_direct_init(const char *path, off_t physaddr, size_t size)
{
    // try to open /dev/vram (or whatever path)
    const char *dev = path ? path : "/dev/vram";
    struct stat st;

    vram_fd = open(dev, O_RDWR | O_SYNC);
    if (vram_fd < 0) {
        // not available — caller should fallback
        return 0;
    }

    if (size)
        vram_size = size;
    if (physaddr)
        vram_phys = physaddr;

    // mmap entire device (offset of 0 corresponds to phys_addr supplied to kernel module)
    vram_map = mmap(NULL, vram_size, PROT_READ | PROT_WRITE, MAP_SHARED, vram_fd, 0);
    if (vram_map == MAP_FAILED) {
        close(vram_fd);
        vram_fd = -1;
        vram_map = NULL;
        return 0;
    }

    // success
    return 1;
}

void vga_direct_close(void)
{
    if (vram_map) {
        munmap(vram_map, vram_size);
        vram_map = NULL;
    }
    if (vram_fd >= 0) {
        close(vram_fd);
        vram_fd = -1;
    }
}

// write a character cell at row, col (0-based). attribute is a byte (foreground/bg)
int vga_direct_putcell(int row, int col, unsigned char ch, unsigned char attr)
{
    if (!vram_map) return 0;
    if (row < 0 || row >= 25 || col < 0 || col >= 80) return 0;
    size_t idx = (row * 80 + col) * 2;
    vram_map[idx] = ch;
    vram_map[idx + 1] = attr;
    return 1;
}

// write bytes to screen line; len <= 80-col
int vga_direct_write(int row, int col, const unsigned char *s, int len, unsigned char attr)
{
    int i;
    if (!vram_map) return 0;
    if (row < 0 || row >= 25 || col < 0 || col >= 80) return 0;
    if (col + len > 80) len = 80 - col;
    for (i = 0; i < len; ++i) {
        size_t idx = (row * 80 + (col + i)) * 2;
        vram_map[idx] = s[i];
        vram_map[idx + 1] = attr;
    }
    return len;
}
