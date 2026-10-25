#ifndef HYPERION_H
#define HYPERION_H

#include <stdint.h>

#define HYPERION_SCREEN_WIDTH  640
#define HYPERION_SCREEN_HEIGHT 250

#ifdef __cplusplus
extern "C" {
#endif

void emu_init(int bios_version, int ram_kb);
void emu_reset(int warm);
void emu_set_cpu_speed(int multiplier);
void emu_set_rtc(int year, int month, int day, int dow, int hour, int min, int sec);
int  emu_step(int instructions);
int  emu_run_frame(void);

int  emu_load_disk(int drive, const uint8_t *data, int len);
void emu_eject_disk(int drive);
void emu_set_write_protect(int drive, int on);
void emu_set_pc_keyboard_port(int on);

void emu_key_event(int hyperion_keycode, int pressed);
void emu_xt_key_event(int xt_scancode, int pressed);
void emu_ascii_keypress(int ascii);

const uint32_t* emu_get_framebuffer(void);
const uint8_t*  emu_get_vram(void);
uint8_t*        emu_get_ram(void);
uint8_t*        emu_get_disk_upload_buffer(int max_size);

/* Debug / status inspection */
uint16_t emu_get_reg(int reg_id);
uint16_t emu_get_ip(void);
uint16_t emu_get_flags(void);
uint8_t  emu_get_video_mode(void);
uint8_t  emu_get_vram_map(void);
uint8_t  emu_get_drive_led(int drive);
uint32_t emu_get_speaker_freq(void);

#ifdef __cplusplus
}
#endif

#endif /* HYPERION_H */
