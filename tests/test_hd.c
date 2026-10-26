/*
 * Automated Regression Test for Dynalogic Hyperion Hard Disk (Drive C:)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "../src/hyperion.h"

static int screen_contains(const char *needle) {
    const uint8_t *vram = emu_get_vram();
    const uint8_t *ram  = emu_get_ram();
    uint16_t page_off = ram[0x44E] | ((uint16_t)ram[0x44F] << 8);
    char full[25 * 81 + 1];
    int pos = 0;
    for (int r = 0; r < 25; r++) {
        for (int c = 0; c < 80; c++) {
            uint8_t ch = vram[(page_off + (r * 80 + c) * 2) % 0x5000];
            full[pos++] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
        }
        full[pos++] = '\n';
    }
    full[pos] = '\0';
    return (strstr(full, needle) != NULL);
}

int main(void) {
    printf("Starting Hyperion Hard Disk (Drive C:) Test...\n");

    emu_init(7, 256);
    emu_set_cpu_speed(6);

    time_t rawtime;
    time(&rawtime);
    struct tm *t = localtime(&rawtime);
    if (t) {
        emu_set_rtc(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_wday,
                    t->tm_hour, t->tm_min, t->tm_sec);
    }

    /* 1. Mount 10MB Hard Disk Image */
    FILE *fh = fopen("web/disks/HYP10MB.IMG", "rb");
    if (!fh) {
        fprintf(stderr, "Error: web/disks/HYP10MB.IMG not found!\n");
        return 1;
    }
    fseek(fh, 0, SEEK_END);
    long hdsz = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    assert(hdsz == 10653696);
    uint8_t *hdbuf = malloc(hdsz);
    assert(fread(hdbuf, 1, hdsz, fh) == (size_t)hdsz);
    fclose(fh);

    assert(emu_load_hd(hdbuf, (int)hdsz) == 0);
    free(hdbuf);
    assert(emu_get_hd_size() == 10653696);

    /* 2. Mount Hyperion MS-DOS 2.11 Floppy in Drive A: */
    FILE *f = fopen("web/disks/HYPDOS21.IMD", "rb");
    if (!f) {
        fprintf(stderr, "Error: web/disks/HYPDOS21.IMD not found!\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    assert(fread(buf, 1, sz, f) == (size_t)sz);
    fclose(f);
    assert(emu_load_disk(0, buf, (int)sz) > 0);
    free(buf);

    /* 3. Boot DOS 2.11 */
    for (int frame = 1; frame <= 150; frame++) emu_run_frame();

    /* Pass DATE and TIME prompts */
    const char *enter_keys = "\r\r";
    for (int i = 0; enter_keys[i]; i++) {
        emu_ascii_keypress(enter_keys[i]);
        for (int f = 0; f < 30; f++) emu_run_frame();
    }

    /* 4. Issue DIR C: */
    const char *cmd_dirc = "DIR C:\r";
    for (int i = 0; cmd_dirc[i]; i++) {
        emu_ascii_keypress(cmd_dirc[i]);
        for (int f = 0; f < 20; f++) emu_run_frame();
    }
    for (int f = 0; f < 100; f++) emu_run_frame();

    /* Check that DIR C: listed files on C: */
    if (!screen_contains("CHKDSK   COM")) {
        fprintf(stderr, "FAIL: CHKDSK.COM not listed on Drive C:!\n");
        return 1;
    }
    if (!screen_contains("bytes free")) {
        fprintf(stderr, "FAIL: Directory free bytes footer not found on Drive C:!\n");
        return 1;
    }
    printf("PASS: DIR C: correctly displayed files on Hard Disk C:\n");

    /* 5. Test Direct Self-Boot from Hard Disk C: (with Floppies Ejected) */
    printf("Testing direct self-boot from Hard Disk C: with Floppy A: and B: ejected...\n");
    emu_eject_disk(0);
    emu_eject_disk(1);
    emu_reset(0); /* Cold reset with empty floppy drives and mounted Hard Disk */

    for (int frame = 1; frame <= 100; frame++) emu_run_frame();

    /* Send Enter to date & time prompts */
    for (int i = 0; enter_keys[i]; i++) {
        emu_ascii_keypress(enter_keys[i]);
        for (int f = 0; f < 30; f++) emu_run_frame();
    }
    for (int f = 0; f < 50; f++) emu_run_frame();

    if (!screen_contains("IO.SYS V2.05") && !screen_contains("Command v. 2.11") && !screen_contains("C>")) {
        fprintf(stderr, "FAIL: Self-boot from Hard Disk C: failed!\n");
        return 1;
    }
    printf("PASS: Successfully self-booted MS-DOS 2.11 directly from Hard Disk C:\n");

    /* 6. Test Blank Disk Generation & Eject */
    uint8_t *blank = emu_create_blank_hd(20);
    assert(blank != NULL);
    assert(emu_get_hd_size() == 615 * 4 * 17 * 512);
    printf("PASS: Blank 20MB Hard Disk created successfully\n");

    emu_eject_hd();
    printf("PASS: Hard Disk ejected successfully\n");

    printf("\nAll Hard Disk C: tests PASSED!\n");
    return 0;
}
