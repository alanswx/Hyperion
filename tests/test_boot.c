#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/hyperion.h"

static void dump_text_screen(const char *label) {
    const uint8_t *vram = emu_get_vram();
    const uint8_t *ram  = emu_get_ram();
    uint16_t page_off = ram[0x44E] | ((uint16_t)ram[0x44F] << 8);
    printf("=== %s (CS:IP=%04X:%04X, Mode=%02X, Map=%d, PageOff=%04X, Equip=%02X, MemKB=%d) ===\n",
           label, emu_get_reg(9), emu_get_ip(), emu_get_video_mode(), emu_get_vram_map(),
           page_off, ram[0x410], ram[0x413] | (ram[0x414] << 8));
    for (int r = 0; r < 25; r++) {
        char line[82];
        int non_space = 0;
        for (int c = 0; c < 80; c++) {
            uint8_t ch = vram[(page_off + (r * 80 + c) * 2) % 0x5000];
            if (ch >= 32 && ch < 127) {
                line[c] = (char)ch;
                if (ch != ' ') non_space = 1;
            } else {
                line[c] = ' ';
            }
        }
        line[80] = '\0';
        if (non_space || r == 0 || r == 24) {
            printf("%2d| %s|\n", r, line);
        }
    }
}

int main(int argc, char **argv) {
    const char *disk_path = (argc > 1) ? argv[1] : "web/disks/HYPDOS21.IMD";
    int bios_ver = (argc > 2) ? atoi(argv[2]) : 7;

    emu_init(bios_ver, 256);

    time_t rawtime;
    time(&rawtime);
    struct tm *t = localtime(&rawtime);
    if (t) {
        emu_set_rtc(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_wday,
                    t->tm_hour, t->tm_min, t->tm_sec);
    }

    FILE *f = fopen(disk_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *buf = malloc(sz);
        fread(buf, 1, sz, f);
        fclose(f);
        int tracks = emu_load_disk(0, buf, (int)sz);
        printf("Loaded %s (%ld bytes, %d tracks) into Drive A:\n", disk_path, sz, tracks);
        free(buf);
    } else {
        printf("No disk loaded (testing ROM BASIC/DISK FAULT)\n");
    }

    for (int frame = 1; frame <= 400; frame++) {
        emu_run_frame();
        if (frame == 50 || frame == 150 || frame == 250 || frame == 400) {
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "Frame %d", frame);
            dump_text_screen(lbl);
        }
    }

    /* Send Enter key a couple of times (for DATE/TIME prompts) and then DIR */
    const char *cmd = "\r\rDIR\r";
    for (int i = 0; cmd[i]; i++) {
        emu_ascii_keypress(cmd[i]);
        for (int f = 0; f < 40; f++) emu_run_frame();
    }
    dump_text_screen("After Enter + DIR");

    return 0;
}
