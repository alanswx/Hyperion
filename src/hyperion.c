/*
 * Dynalogic Hyperion (1983) Emulator Core
 *
 * CPU execution engine derived from 8086tiny (MIT License, Copyright 2013-14 Adrian Cable)
 * adapted for cycle/bus-accurate Dynalogic Hyperion hardware emulation & WebAssembly.
 */

#include "hyperion.h"
#include "decode_tables.h"
#include "roms.h"

#ifdef __wasm__
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

/* 8086 CPU Constants */
#define REGS_BASE       0x100000
#define MEM_ARRAY_SIZE  0x101000

#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7
#define REG_ES 8
#define REG_CS 9
#define REG_SS 10
#define REG_DS 11
#define REG_ZERO 12
#define REG_SCRATCH 13

#define REG_AL 0
#define REG_AH 1
#define REG_CL 2
#define REG_CH 3
#define REG_DL 4
#define REG_DH 5
#define REG_BL 6
#define REG_BH 7

#define FLAG_CF 40
#define FLAG_PF 41
#define FLAG_AF 42
#define FLAG_ZF 43
#define FLAG_SF 44
#define FLAG_TF 45
#define FLAG_IF 46
#define FLAG_DF 47
#define FLAG_OF 48

#define TABLE_XLAT_OPCODE        8
#define TABLE_XLAT_SUBFUNCTION   9
#define TABLE_STD_FLAGS          10
#define TABLE_PARITY_FLAG        11
#define TABLE_BASE_INST_SIZE     12
#define TABLE_I_W_SIZE           13
#define TABLE_I_MOD_SIZE         14
#define TABLE_COND_JUMP_DECODE_A 15
#define TABLE_COND_JUMP_DECODE_B 16
#define TABLE_COND_JUMP_DECODE_C 17
#define TABLE_COND_JUMP_DECODE_D 18
#define TABLE_FLAGS_BITFIELDS    19

#define FLAGS_UPDATE_SZP      1
#define FLAGS_UPDATE_AO_ARITH 2
#define FLAGS_UPDATE_OC_LOGIC 4

/* Memory & Hardware State */
#define VRAM_SIZE       0x5000
#define UPLOAD_BUF_SIZE (512 * 1024)

#ifdef __wasm__
#define mem             ((uint8_t *)0x020000)
#define vram            ((uint8_t *)0x121000)
#define framebuffer     ((uint32_t *)0x126000)
#define disk_upload_buf ((uint8_t *)0x1C3000)
#define floppy_data     ((uint8_t (*)[FDC_MAX_CYLS][FDC_MAX_HEADS][FDC_MAX_SECTORS][FDC_SECTOR_SIZE])0x243000)
#else
static uint8_t  mem[MEM_ARRAY_SIZE];
static uint8_t  vram[VRAM_SIZE]; /* 20 KB CRT Display RAM */
static uint32_t framebuffer[HYPERION_SCREEN_WIDTH * HYPERION_SCREEN_HEIGHT];
static uint8_t  disk_upload_buf[UPLOAD_BUF_SIZE];
#endif

static const uint8_t *active_bios_rom = hyperion_bios_c7_rom;
static uint32_t ram_limit = 0x40000; /* Default 256 KB (or 0xA0000 for 640 KB) */

static uint8_t  *regs8;
static uint16_t *regs16;
static uint16_t reg_ip;
static uint16_t seg_override;
static uint8_t  i_rm, i_w, i_reg, i_mod, i_mod_size, i_d, i_reg4bit;
static uint8_t  raw_opcode_id, xlat_opcode_id, extra, rep_mode;
static uint8_t  seg_override_en, rep_override_en, trap_flag, scratch_uchar;
static uint32_t op_source, op_dest, rm_addr, op_to_addr, op_from_addr;
static uint32_t i_data0, i_data1, i_data2, scratch_uint, scratch2_uint;
static uint32_t set_flags_type, inst_counter;
static int32_t  op_result, scratch_int;

/* Hyperion Video / CRT State */
static uint8_t  crt_ctrl_reg = 0;   /* Memory-mapped at A000:0000 */
static uint8_t  vram_map_reg = 0;   /* Memory-mapped at A800:0000 (0=off, 1=both, 2=B800, 3=B000) */
static uint8_t  crtc_regs[32];      /* Ports 0x180 - 0x19F */
static uint8_t  mc6845_idx = 0;
static uint8_t  mc6845_regs[32];
static uint8_t  cga_mode_reg = 0;   /* Port 0x3D8 */
static uint8_t  cga_color_reg = 0;  /* Port 0x3D9 */
static uint8_t  status_3da_first = 1;
static uint8_t  status_3da_cycle = 0;
static uint32_t blink_counter = 0;

/* 8259A PIC State (Ports 0x20 - 0x21) */
static uint8_t  pic_irr = 0;
static uint8_t  pic_isr = 0;
static uint8_t  pic_imr = 0xFF;
static uint8_t  pic_icw_step = 0;
static uint8_t  pic_vector_base = 0x08;

/* 8253 PIT State (Ports 0x40 - 0x43) & Speaker (Port 0x61) */
static uint16_t pit_reload[3] = { 0, 0, 0 };
static uint16_t pit_count[3]  = { 0, 0, 0 };
static uint8_t  pit_FlipFlop[3] = { 0, 0, 0 };
static uint8_t  pit_mode[3] = { 0, 0, 0 };
static uint8_t  port_61 = 0x20; /* Bit 5 = 1 (2 floppy drives present) */
static uint8_t  port_c8 = 0x00; /* System Control Register */

/* 8237A DMA State (Ports 0x00 - 0x0F, Page 0x80 - 0x83) */
static uint16_t dma_addr[4];
static uint16_t dma_count[4];
static uint8_t  dma_page[4];
static uint8_t  dma_flipflop = 0;
static uint8_t  dma_mode[4];
static uint8_t  dma_mask = 0x0F;

/* 8272A FDC State (Ports 0xC0 - 0xC1) */
#define FDC_MAX_DRIVES   2
#define FDC_MAX_CYLS     42
#define FDC_MAX_HEADS    2
#define FDC_MAX_SECTORS  10
#define FDC_SECTOR_SIZE  512

static uint8_t  floppy_mounted[FDC_MAX_DRIVES];
static uint8_t  floppy_spt[FDC_MAX_DRIVES];
#ifndef __wasm__
static uint8_t  floppy_data[FDC_MAX_DRIVES][FDC_MAX_CYLS][FDC_MAX_HEADS][FDC_MAX_SECTORS][FDC_SECTOR_SIZE];
#endif
static uint8_t  fdc_cmd_buf[16];
static uint8_t  fdc_cmd_len = 0;
static uint8_t  fdc_cmd_expected = 0;
static uint8_t  fdc_res_buf[16];
static uint8_t  fdc_res_len = 0;
static uint8_t  fdc_res_pos = 0;
static uint8_t  fdc_cur_cyl[FDC_MAX_DRIVES];
static uint8_t  fdc_int_st0 = 0;
static uint8_t  fdc_int_pcn = 0;
static uint8_t  fdc_int_pending = 0;
static int32_t  fdc_irq_delay = 0;
static uint32_t drive_led_timer[FDC_MAX_DRIVES];

/* Keyboard State (Port 0xE0) */
#define KBD_QUEUE_SIZE 64
static uint8_t  kbd_queue[KBD_QUEUE_SIZE];
static uint8_t  kbd_q_head = 0;
static uint8_t  kbd_q_tail = 0;
static uint8_t  kbd_latch = 0x00; /* Value CPU sees after NOT AL */
static uint8_t  kbd_last_out = 0xFF;
static uint8_t  kbd_waiting_ack = 0;
static int32_t  kbd_irq_delay = 0;

/* 8255 PPI + MSM5832 Real-Time Clock (Ports 0x140 - 0x143) */
static uint8_t  ppi_port_a = 0x20;
static uint8_t  ppi_port_b = 0xFF;
static uint8_t  ppi_port_c = 0x00;
static uint8_t  ppi_ctrl   = 0x80;
static uint8_t  rtc_addr   = 0;
static uint8_t  rtc_regs[16] = {
    0, 0,       /* Sec: 00 */
    0, 3,       /* Min: 30 */
    9, 8 | 0,   /* Hour: 09 (24h mode bit 3 = 1) */
    5,          /* Day of week */
    3, 2,       /* Day: 23 */
    0, 1,       /* Month: 10 */
    3, 8        /* Year: 83 (1983) */
};

/* Z80-SIO (Ports 0x100 - 0x103) & Parallel Port (0x378 - 0x37A) */
static uint8_t  sio_wr_idx[2] = { 0, 0 };
static uint8_t  sio_wr[2][8];
static uint8_t  lpt_data = 0;
static uint8_t  lpt_ctrl = 0x0C;

static void pic_request_irq(int irq) {
    pic_irr |= (uint8_t)(1 << irq);
}

static void kbd_push_code(uint8_t code) {
    uint8_t next = (kbd_q_tail + 1) & (KBD_QUEUE_SIZE - 1);
    if (next != kbd_q_head) {
        kbd_queue[kbd_q_tail] = code;
        kbd_q_tail = next;
    }
}

/* Physical 20-bit + Register Memory Access */
static inline uint8_t mem_read8(uint32_t addr) {
    if (addr >= REGS_BASE) {
        return mem[addr];
    }
    addr &= 0xFFFFF;
    if (addr < ram_limit) {
        return mem[addr];
    }
    if (addr >= 0xA0000 && addr < 0xA8000) {
        return crt_ctrl_reg;
    }
    if (addr >= 0xA8000 && addr < 0xB0000) {
        return vram_map_reg;
    }
    if (addr >= 0xB0000 && addr < 0xB5000) {
        if (vram_map_reg == 1 || vram_map_reg == 3)
            return vram[addr - 0xB0000];
        return 0xFF;
    }
    if (addr >= 0xB8000 && addr < 0xBD000) {
        if (vram_map_reg == 1 || vram_map_reg == 2)
            return vram[addr - 0xB8000];
        return 0xFF;
    }
    if (addr >= 0xFE000) {
        return active_bios_rom[addr - 0xFE000];
    }
    return 0xFF;
}

static inline void mem_write8(uint32_t addr, uint8_t val) {
    if (addr >= REGS_BASE) {
        mem[addr] = val;
        return;
    }
    addr &= 0xFFFFF;
    if (addr < ram_limit) {
        mem[addr] = val;
        return;
    }
    if (addr >= 0xA0000 && addr < 0xA8000) {
        crt_ctrl_reg = val;
        return;
    }
    if (addr >= 0xA8000 && addr < 0xB0000) {
        vram_map_reg = val & 0x03;
        return;
    }
    if (addr >= 0xB0000 && addr < 0xB5000) {
        if (vram_map_reg == 1 || vram_map_reg == 3)
            vram[addr - 0xB0000] = val;
        return;
    }
    if (addr >= 0xB8000 && addr < 0xBD000) {
        if (vram_map_reg == 1 || vram_map_reg == 2)
            vram[addr - 0xB8000] = val;
        return;
    }
}

static inline uint16_t mem_read16(uint32_t addr) {
    if (addr >= REGS_BASE) {
        return (uint16_t)(mem[addr] | ((uint16_t)mem[addr + 1] << 8));
    }
    return (uint16_t)(mem_read8(addr) | ((uint16_t)mem_read8(addr + 1) << 8));
}

static inline void mem_write16(uint32_t addr, uint16_t val) {
    if (addr >= REGS_BASE) {
        mem[addr]     = (uint8_t)(val & 0xFF);
        mem[addr + 1] = (uint8_t)(val >> 8);
        return;
    }
    mem_write8(addr, (uint8_t)(val & 0xFF));
    mem_write8(addr + 1, (uint8_t)(val >> 8));
}

static inline uint32_t m_read(uint32_t addr) {
    return i_w ? mem_read16(addr) : mem_read8(addr);
}

/* 8272A FDC Command Execution */
static uint8_t fdc_expected_bytes(uint8_t cmd) {
    uint8_t op = cmd & 0x1F;
    switch (op) {
        case 0x03: return 3; /* Specify */
        case 0x04: return 2; /* Sense Drive Status */
        case 0x05: return 9; /* Write Data */
        case 0x06: return 9; /* Read Data */
        case 0x07: return 2; /* Recalibrate */
        case 0x08: return 1; /* Sense Interrupt Status */
        case 0x0A: return 2; /* Read ID */
        case 0x0D: return 6; /* Format Track */
        case 0x0F: return 3; /* Seek */
        default:   return 1;
    }
}

static void fdc_execute_command(void) {
    uint8_t cmd = fdc_cmd_buf[0];
    uint8_t op  = cmd & 0x1F;
    fdc_res_len = 0;
    fdc_res_pos = 0;

    if (op == 0x03) {
        /* Specify: no interrupt, no result phase */
        return;
    }
    if (op == 0x08) {
        /* Sense Interrupt Status */
        if (fdc_int_pending) {
            fdc_res_buf[0] = fdc_int_st0;
            fdc_res_buf[1] = fdc_int_pcn;
            fdc_int_pending = 0;
        } else {
            fdc_res_buf[0] = 0x80;
            fdc_res_buf[1] = 0x00;
        }
        fdc_res_len = 2;
        return;
    }
    if (op == 0x04) {
        /* Sense Drive Status */
        uint8_t drv = fdc_cmd_buf[1] & 1;
        uint8_t hd  = (fdc_cmd_buf[1] >> 2) & 1;
        uint8_t st3 = 0x28 | (hd << 2) | drv;
        if (fdc_cur_cyl[drv] == 0) st3 |= 0x10; /* Track 0 */
        fdc_res_buf[0] = st3;
        fdc_res_len = 1;
        return;
    }
    if (op == 0x07) {
        /* Recalibrate */
        uint8_t drv = fdc_cmd_buf[1] & 1;
        fdc_cur_cyl[drv] = 0;
        fdc_int_st0 = 0x20 | drv;
        fdc_int_pcn = 0;
        fdc_int_pending = 1;
        fdc_irq_delay = 15;
        return;
    }
    if (op == 0x0F) {
        /* Seek */
        uint8_t drv = fdc_cmd_buf[1] & 1;
        uint8_t ncn = fdc_cmd_buf[2];
        if (ncn >= FDC_MAX_CYLS) ncn = FDC_MAX_CYLS - 1;
        fdc_cur_cyl[drv] = ncn;
        fdc_int_st0 = 0x20 | (fdc_cmd_buf[1] & 7);
        fdc_int_pcn = ncn;
        fdc_int_pending = 1;
        fdc_irq_delay = 15;
        return;
    }
    if (op == 0x0A) {
        /* Read ID */
        uint8_t drv = fdc_cmd_buf[1] & 1;
        uint8_t hd  = (fdc_cmd_buf[1] >> 2) & 1;
        drive_led_timer[drv] = 15;
        if (!floppy_mounted[drv]) {
            fdc_res_buf[0] = 0x40 | (hd << 2) | drv;
            fdc_res_buf[1] = 0x01; /* Missing address mark */
            fdc_res_buf[2] = 0x00;
        } else {
            fdc_res_buf[0] = (hd << 2) | drv;
            fdc_res_buf[1] = 0x00;
            fdc_res_buf[2] = 0x00;
        }
        fdc_res_buf[3] = fdc_cur_cyl[drv];
        fdc_res_buf[4] = hd;
        fdc_res_buf[5] = 1;
        fdc_res_buf[6] = 2;
        fdc_res_len = 7;
        fdc_irq_delay = 15;
        return;
    }
    if (op == 0x06 || op == 0x05) {
        /* Read Data (0x06) or Write Data (0x05) */
        uint8_t drv = fdc_cmd_buf[1] & 1;
        uint8_t c   = fdc_cmd_buf[2];
        uint8_t h   = fdc_cmd_buf[3] & 1;
        uint8_t r   = fdc_cmd_buf[4];
        uint8_t n   = fdc_cmd_buf[5];
        uint8_t eot = fdc_cmd_buf[6];
        drive_led_timer[drv] = 20;

        if (!floppy_mounted[drv] || c >= FDC_MAX_CYLS || r < 1) {
            fdc_res_buf[0] = 0x40 | (h << 2) | drv;
            fdc_res_buf[1] = 0x04; /* No Data / Not Ready */
            fdc_res_buf[2] = 0x00;
            fdc_res_buf[3] = c;
            fdc_res_buf[4] = h;
            fdc_res_buf[5] = r;
            fdc_res_buf[6] = n;
            fdc_res_len = 7;
            fdc_irq_delay = 15;
            return;
        }

        uint8_t max_spt = floppy_spt[drv] ? floppy_spt[drv] : 8;
        if (r > max_spt) {
            /* Sector not found (e.g., DOS probing sector 9 on an 8-sector disk) */
            fdc_res_buf[0] = 0x40 | (h << 2) | drv;
            fdc_res_buf[1] = 0x04; /* Sector Not Found */
            fdc_res_buf[2] = 0x00;
            fdc_res_buf[3] = c;
            fdc_res_buf[4] = h;
            fdc_res_buf[5] = r;
            fdc_res_buf[6] = n;
            fdc_res_len = 7;
            fdc_irq_delay = 15;
            return;
        }

        uint32_t phys_addr = ((uint32_t)(dma_page[2] & 0x0F) << 16) | dma_addr[2];
        uint32_t total_bytes = (uint32_t)dma_count[2] + 1;
        uint32_t num_sectors = total_bytes / FDC_SECTOR_SIZE;
        if (num_sectors == 0) num_sectors = 1;

        /* Check if DMA channel 2 is in Verify mode ((dma_mode[2] & 0x0C) == 0x00) */
        int is_verify = ((dma_mode[2] & 0x0C) == 0x00);
        uint8_t cur_c = c;
        uint8_t cur_h = h;
        uint8_t cur_r = r;
        uint8_t err_st1 = 0;

        for (uint32_t s = 0; s < num_sectors; s++) {
            if (cur_r > max_spt || cur_c >= FDC_MAX_CYLS) {
                err_st1 = 0x04; /* Sector Not Found */
                break;
            }
            uint8_t *sec_ptr = floppy_data[drv][cur_c][cur_h][cur_r - 1];
            if (op == 0x06) {
                if (!is_verify) {
                    for (uint32_t b = 0; b < FDC_SECTOR_SIZE; b++) {
                        mem_write8(phys_addr++, sec_ptr[b]);
                    }
                } else {
                    phys_addr += FDC_SECTOR_SIZE;
                }
            } else {
                for (uint32_t b = 0; b < FDC_SECTOR_SIZE; b++) {
                    sec_ptr[b] = mem_read8(phys_addr++);
                }
            }
            cur_r++;
        }

        dma_addr[2] = (uint16_t)(phys_addr & 0xFFFF);
        dma_count[2] = 0xFFFF;

        if (err_st1) {
            fdc_res_buf[0] = 0x40 | (cur_h << 2) | drv;
            fdc_res_buf[1] = err_st1;
            fdc_res_buf[2] = 0x00;
            fdc_res_buf[3] = cur_c;
            fdc_res_buf[4] = cur_h;
            fdc_res_buf[5] = cur_r;
            fdc_res_buf[6] = n;
        } else {
            uint8_t end_c = cur_c;
            uint8_t end_r = cur_r;
            if (end_r > eot) {
                end_c = cur_c + 1;
                end_r = 1;
            }
            fdc_res_buf[0] = (cur_h << 2) | drv;
            fdc_res_buf[1] = 0x00;
            fdc_res_buf[2] = 0x00;
            fdc_res_buf[3] = end_c;
            fdc_res_buf[4] = cur_h;
            fdc_res_buf[5] = end_r;
            fdc_res_buf[6] = n;
        }
        fdc_res_len = 7;
        fdc_irq_delay = 15;
        return;
    }
    if (op == 0x0D) {
        /* Format Track */
        uint8_t drv = fdc_cmd_buf[1] & 1;
        uint8_t hd  = (fdc_cmd_buf[1] >> 2) & 1;
        uint8_t n   = fdc_cmd_buf[2];
        uint8_t sc  = fdc_cmd_buf[3];
        uint8_t fill = fdc_cmd_buf[5];
        drive_led_timer[drv] = 20;
        uint8_t cyl = fdc_cur_cyl[drv];
        if (cyl < FDC_MAX_CYLS && sc <= FDC_MAX_SECTORS) {
            floppy_spt[drv] = sc;
            floppy_mounted[drv] = 1;
            for (uint8_t s = 0; s < sc; s++) {
                for (int b = 0; b < FDC_SECTOR_SIZE; b++) {
                    floppy_data[drv][cyl][hd][s][b] = fill;
                }
            }
        }
        fdc_res_buf[0] = (hd << 2) | drv;
        fdc_res_buf[1] = 0x00;
        fdc_res_buf[2] = 0x00;
        fdc_res_buf[3] = cyl;
        fdc_res_buf[4] = hd;
        fdc_res_buf[5] = sc + 1;
        fdc_res_buf[6] = n;
        fdc_res_len = 7;
        fdc_irq_delay = 15;
        return;
    }
}

/* I/O Port Read & Write */
static uint8_t io_read8(uint16_t port) {
    /* 8237A DMAC (0x00 - 0x0F) */
    if (port <= 0x0F) {
        if (port < 0x08) {
            uint8_t ch = port >> 1;
            uint16_t val = (port & 1) ? dma_count[ch] : dma_addr[ch];
            uint8_t res = dma_flipflop ? (uint8_t)(val >> 8) : (uint8_t)(val & 0xFF);
            dma_flipflop ^= 1;
            return res;
        }
        if (port == 0x08) return 0x0F; /* DMA Status: TC reached */
        return 0x00;
    }

    /* 8259A PIC (0x20 - 0x21) */
    if (port == 0x20) return pic_irr;
    if (port == 0x21) return pic_imr;

    /* 8253 PIT (0x40 - 0x43) */
    if (port >= 0x40 && port <= 0x42) {
        uint8_t ch = port - 0x40;
        pit_count[ch] -= 17;
        uint8_t res = pit_FlipFlop[ch] ? (uint8_t)(pit_count[ch] >> 8) : (uint8_t)(pit_count[ch] & 0xFF);
        pit_FlipFlop[ch] ^= 1;
        return res;
    }

    /* Speaker / System Status Register (0x61) */
    if (port == 0x61) {
        /*
         * Bits 0-1: Speaker control (last written)
         * Bits 2-3: Floppy drive 0/1 motor status from port 0xC8 bits 0-1
         * Bit 5 (0x20): 1 = Dual floppy drives installed
         * Bit 7 (0x80): 0 = No parity error
         */
        uint8_t motor_bits = (port_c8 & 0x03) << 2;
        return (port_61 & 0x03) | motor_bits | 0x20;
    }

    /* 8272A FDC (0xC0 = Main Status, 0xC1 = Data FIFO) */
    if (port == 0xC0) {
        if (fdc_irq_delay > 0) {
            return 0x10; /* Command busy, RQM=0 until IRQ fires */
        }
        uint8_t msr = 0x80; /* RQM = 1 */
        if (fdc_res_pos < fdc_res_len) {
            msr |= 0x40 | 0x10; /* DIO = 1 (FDC->CPU), CB = 1 (Busy) */
        } else if (fdc_cmd_len > 0) {
            msr |= 0x10; /* CB = 1 while receiving command bytes */
        }
        return msr;
    }
    if (port == 0xC1) {
        if (fdc_res_pos < fdc_res_len) {
            uint8_t b = fdc_res_buf[fdc_res_pos++];
            if (fdc_res_pos >= fdc_res_len) {
                fdc_res_len = 0;
                fdc_res_pos = 0;
            }
            return b;
        }
        return 0x00;
    }

    /* System Control Register (0xC8) */
    if (port == 0xC8) return port_c8;

    /* Keyboard Data Port (0xE0) - Hardware inverted! */
    if (port == 0xE0) {
        return (uint8_t)(~kbd_latch);
    }

    /* Z80-SIO Serial & Modem (0x100 - 0x103) */
    if (port == 0x100 || port == 0x101) {
        return 0x00;
    }
    if (port == 0x102 || port == 0x103) {
        uint8_t ch = port & 1;
        uint8_t reg = sio_wr_idx[ch];
        sio_wr_idx[ch] = 0;
        if (reg == 0) {
            /* RR0: Bit 2 = Tx Buffer Empty, Bit 3 = DCD, Bit 5 = CTS */
            return 0x2C;
        }
        if (reg == 1) {
            /* RR1: Bit 0 = All Sent */
            return 0x01;
        }
        return 0x00;
    }

    /* 8255 PPI + MSM5832 Real-Time Clock (0x140 - 0x143) */
    if (port == 0x140) return ppi_port_a;
    if (port == 0x141) return ppi_port_b;
    if (port == 0x142) {
        if ((ppi_port_a & 0x02) && (ppi_ctrl & 0x01)) {
            /* RTC READ active and Port C lower nibble in input mode */
            return (ppi_port_c & 0xF0) | (rtc_regs[rtc_addr & 0x0F] & 0x0F);
        }
        return ppi_port_c;
    }
    if (port == 0x143) return ppi_ctrl;

    /* Hyperion CRT Controller Registers (0x180 - 0x19A, 0x1B8 - 0x1BA) */
    if (port >= 0x180 && port <= 0x19F) {
        return crtc_regs[port - 0x180];
    }
    if (port >= 0x1B8 && port <= 0x1BA) {
        return 0x00;
    }

    /* Parallel Printer Port (0x378 - 0x37A) */
    if (port == 0x378) return lpt_data;
    if (port == 0x379) return 0xDF; /* Not busy, ACK, selected, no error */
    if (port == 0x37A) return lpt_ctrl;

    /* MC6845 / CGA / MDA Compatibility Ports (0x3B0 - 0x3DF) */
    if (port == 0x3B5 || port == 0x3D5) {
        return mc6845_regs[mc6845_idx & 0x1F];
    }
    if (port == 0x3BA || port == 0x3DA) {
        if (status_3da_first) {
            status_3da_first = 0;
            return 0x00; /* Must be 0 on first POST check at FE00:0070 */
        }
        status_3da_cycle = (status_3da_cycle + 1) & 3;
        if (status_3da_cycle == 0) return 0x00;
        if (status_3da_cycle == 1) return 0x01; /* HBlank */
        if (status_3da_cycle == 2) return 0x08; /* VSync */
        return 0x09;
    }

    return 0xFF;
}

static void io_write8(uint16_t port, uint8_t val) {
    /* 8237A DMAC (0x00 - 0x0F) */
    if (port <= 0x0F) {
        if (port < 0x08) {
            uint8_t ch = port >> 1;
            if (port & 1) {
                if (dma_flipflop) dma_count[ch] = (dma_count[ch] & 0x00FF) | ((uint16_t)val << 8);
                else              dma_count[ch] = (dma_count[ch] & 0xFF00) | val;
            } else {
                if (dma_flipflop) dma_addr[ch] = (dma_addr[ch] & 0x00FF) | ((uint16_t)val << 8);
                else              dma_addr[ch] = (dma_addr[ch] & 0xFF00) | val;
            }
            dma_flipflop ^= 1;
        } else if (port == 0x0A) {
            uint8_t ch = val & 3;
            if (val & 4) dma_mask |= (1 << ch);
            else         dma_mask &= ~(1 << ch);
        } else if (port == 0x0B) {
            dma_mode[val & 3] = val;
        } else if (port == 0x0C) {
            dma_flipflop = 0;
        } else if (port == 0x0D) {
            dma_flipflop = 0;
            dma_mask = 0x0F;
        } else if (port == 0x0E) {
            dma_mask = 0x00;
        } else if (port == 0x0F) {
            dma_mask = val & 0x0F;
        }
        return;
    }

    /* 8259A PIC (0x20 - 0x21) */
    if (port == 0x20) {
        if (val & 0x10) {
            /* ICW1 */
            pic_icw_step = 1;
            pic_imr = 0;
            pic_isr = 0;
            pic_irr = 0;
        } else if ((val & 0x18) == 0x00) {
            /* OCW2: EOI */
            if (val & 0x20) {
                pic_isr = 0;
            }
        }
        return;
    }
    if (port == 0x21) {
        if (pic_icw_step == 1) {
            pic_vector_base = val & 0xF8;
            pic_icw_step = 2;
        } else if (pic_icw_step == 2) {
            pic_icw_step = 0;
        } else {
            pic_imr = val;
        }
        return;
    }

    /* 8253 PIT (0x40 - 0x43) */
    if (port >= 0x40 && port <= 0x42) {
        uint8_t ch = port - 0x40;
        if (pit_FlipFlop[ch]) {
            pit_reload[ch] = (pit_reload[ch] & 0x00FF) | ((uint16_t)val << 8);
            pit_count[ch] = pit_reload[ch];
        } else {
            pit_reload[ch] = (pit_reload[ch] & 0xFF00) | val;
        }
        pit_FlipFlop[ch] ^= 1;
        return;
    }
    if (port == 0x43) {
        uint8_t ch = (val >> 6) & 3;
        if (ch < 3) {
            pit_FlipFlop[ch] = 0;
            pit_mode[ch] = (val >> 1) & 7;
        }
        return;
    }

    /* Speaker / System Status (0x61) */
    if (port == 0x61) {
        port_61 = (port_61 & 0xFC) | (val & 0x03);
        return;
    }

    /* DMA Page Registers (0x80 - 0x83) */
    if (port >= 0x80 && port <= 0x83) {
        static const uint8_t page_map[4] = { 0, 2, 3, 1 };
        dma_page[page_map[port & 3]] = val & 0x0F;
        return;
    }

    /* 8272A FDC Data Register (0xC1) */
    if (port == 0xC1) {
        if (fdc_cmd_len == 0) {
            fdc_cmd_expected = fdc_expected_bytes(val);
        }
        if (fdc_cmd_len < sizeof(fdc_cmd_buf)) {
            fdc_cmd_buf[fdc_cmd_len++] = val;
        }
        if (fdc_cmd_len >= fdc_cmd_expected) {
            fdc_execute_command();
            fdc_cmd_len = 0;
        }
        return;
    }

    /* System Control Register (0xC8) */
    if (port == 0xC8) {
        uint8_t prev = port_c8;
        port_c8 = val;
        /* Rising edge on bit 3 (0x08) releases 8272A FDC reset and triggers IRQ6 */
        if (!(prev & 0x08) && (val & 0x08)) {
            fdc_cmd_len = 0;
            fdc_res_len = 0;
            fdc_res_pos = 0;
            fdc_int_st0 = 0xC0; /* Polling ready state change after reset */
            fdc_int_pcn = 0x00;
            fdc_int_pending = 1;
            fdc_irq_delay = 15;
        }
        return;
    }

    /* Keyboard Control/Ack Port (0xE0) */
    if (port == 0xE0) {
        if (kbd_last_out == 0x00 && val == 0xFF) {
            /* Keyboard reset complete: self-test returns 0xAA */
            kbd_q_head = 0;
            kbd_q_tail = 0;
            kbd_latch = 0xAA;
            kbd_waiting_ack = 0;
        } else if (val & 0x80) {
            /* Acknowledge key read */
            kbd_waiting_ack = 0;
            if (kbd_q_head != kbd_q_tail) {
                kbd_irq_delay = 50;
            }
        }
        kbd_last_out = val;
        return;
    }

    /* Z80-SIO (0x100 - 0x103) */
    if (port == 0x102 || port == 0x103) {
        uint8_t ch = port & 1;
        uint8_t reg = sio_wr_idx[ch];
        if (reg == 0) {
            sio_wr_idx[ch] = val & 0x07;
        } else {
            sio_wr[ch][reg] = val;
            sio_wr_idx[ch] = 0;
        }
        return;
    }

    /* 8255 PPI + MSM5832 RTC (0x140 - 0x143) */
    if (port == 0x140) {
        uint8_t prev = ppi_port_a;
        ppi_port_a = val;
        /* Falling edge on bit 3 (0x08): Latch RTC address from Port C lower 4 bits */
        if ((prev & 0x08) && !(val & 0x08)) {
            rtc_addr = ppi_port_c & 0x0F;
        }
        /* Falling edge on bit 2 (0x04): Write Port C lower 4 bits into RTC register */
        if ((prev & 0x04) && !(val & 0x04)) {
            rtc_regs[rtc_addr & 0x0F] = ppi_port_c & 0x0F;
        }
        return;
    }
    if (port == 0x141) { ppi_port_b = val; return; }
    if (port == 0x142) { ppi_port_c = val; return; }
    if (port == 0x143) { ppi_ctrl   = val; return; }

    /* Hyperion CRT Controller Registers (0x180 - 0x19F) */
    if (port >= 0x180 && port <= 0x19F) {
        crtc_regs[port - 0x180] = val;
        return;
    }

    /* Parallel Printer Port (0x378 - 0x37A) */
    if (port == 0x378) { lpt_data = val; return; }
    if (port == 0x37A) { lpt_ctrl = val; return; }

    /* MC6845 / CGA / MDA Compatibility Ports (0x3B0 - 0x3DF) */
    if (port == 0x3B4 || port == 0x3D4) {
        mc6845_idx = val & 0x1F;
        return;
    }
    if (port == 0x3B5 || port == 0x3D5) {
        mc6845_regs[mc6845_idx & 0x1F] = val;
        return;
    }
    if (port == 0x3D8) { cga_mode_reg = val; return; }
    if (port == 0x3D9) { cga_color_reg = val; return; }
}

/* 8086 CPU Helpers */
#define GET_REG_ADDR(reg_id) (REGS_BASE + (i_w ? 2 * (reg_id) : ((2 * (reg_id) + ((reg_id) / 4)) & 7)))
#define TOP_BIT              (8 * (i_w + 1))
#define SEGREG(reg_seg, reg_ofs, op) (16 * (uint32_t)regs16[reg_seg] + (uint16_t)(op regs16[reg_ofs]))
#define SIGN_OF(a)           (1 & ((i_w ? (int16_t)(a) : (int8_t)(a)) >> (TOP_BIT - 1)))
#define INDEX_INC(reg_id)    (regs16[reg_id] -= (uint16_t)((2 * regs8[FLAG_DF] - 1) * (i_w + 1)))

static inline uint8_t set_CF(int new_CF) {
    return regs8[FLAG_CF] = (uint8_t)(!!new_CF);
}

static inline uint8_t set_AF(int new_AF) {
    return regs8[FLAG_AF] = (uint8_t)(!!new_AF);
}

static inline uint8_t set_OF(int new_OF) {
    return regs8[FLAG_OF] = (uint8_t)(!!new_OF);
}

static inline uint8_t set_AF_OF_arith(void) {
    set_AF((op_source ^= op_dest ^ (uint32_t)op_result) & 0x10);
    if ((uint32_t)op_result == op_dest)
        return set_OF(0);
    else
        return set_OF(1 & (regs8[FLAG_CF] ^ (op_source >> (TOP_BIT - 1))));
}

static inline void make_flags(void) {
    scratch_uint = 0xF002;
    for (int i = 9; i--;)
        scratch_uint += (uint32_t)regs8[FLAG_CF + i] << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i];
}

static inline void set_flags(int new_flags) {
    for (int i = 9; i--;)
        regs8[FLAG_CF + i] = (uint8_t)(!!((1 << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i]) & new_flags));
}

static inline void set_opcode(uint8_t opcode) {
    raw_opcode_id  = opcode;
    xlat_opcode_id = bios_table_lookup[TABLE_XLAT_OPCODE][opcode];
    extra          = bios_table_lookup[TABLE_XLAT_SUBFUNCTION][opcode];
    i_mod_size     = bios_table_lookup[TABLE_I_MOD_SIZE][opcode];
    set_flags_type = bios_table_lookup[TABLE_STD_FLAGS][opcode];
}

static inline uint32_t m_op(uint32_t dest_addr, char op, uint32_t src_val) {
    if (i_w) {
        uint16_t d = mem_read16(dest_addr);
        uint16_t s = (uint16_t)src_val;
        uint16_t r = d;
        op_dest   = d;
        op_source = s;
        switch (op) {
            case '=': r = s; break;
            case '+': r = d + s; break;
            case '-': r = d - s; break;
            case '|': r = d | s; break;
            case '&': r = d & s; break;
            case '^': r = d ^ s; break;
            case '~': r = ~s; break;
            case 'n': r = (uint16_t)(-(int16_t)s); break;
            case '<': r = (uint16_t)(d << s); break;
            case '>': r = (uint16_t)(d >> s); break;
            case 'A': r = d + s + regs8[FLAG_CF]; break;
            case 'S': r = d - (s + regs8[FLAG_CF]); break;
            case 'c': r = d - s; op_result = r; return r;
            case 't': r = d & s; op_result = r; return r;
        }
        op_result = r;
        mem_write16(dest_addr, r);
        return r;
    } else {
        uint8_t d = mem_read8(dest_addr);
        uint8_t s = (uint8_t)src_val;
        uint8_t r = d;
        op_dest   = d;
        op_source = s;
        switch (op) {
            case '=': r = s; break;
            case '+': r = d + s; break;
            case '-': r = d - s; break;
            case '|': r = d | s; break;
            case '&': r = d & s; break;
            case '^': r = d ^ s; break;
            case '~': r = ~s; break;
            case 'n': r = (uint8_t)(-(int8_t)s); break;
            case '<': r = (uint8_t)(d << s); break;
            case '>': r = (uint8_t)(d >> s); break;
            case 'A': r = d + s + regs8[FLAG_CF]; break;
            case 'S': r = d - (s + regs8[FLAG_CF]); break;
            case 'c': r = d - s; op_result = r; return r;
            case 't': r = d & s; op_result = r; return r;
        }
        op_result = r;
        mem_write8(dest_addr, r);
        return r;
    }
}

#define MEM_OP(dest_addr, op_char, src_addr) m_op((dest_addr), (op_char), m_read(src_addr))
#define OP(op_char)                          MEM_OP(op_to_addr, (op_char), op_from_addr)

static inline void r_m_push(uint16_t val) {
    i_w = 1;
    regs16[REG_SP] -= 2;
    m_op(SEGREG(REG_SS, REG_SP,), '=', val);
}

static inline uint16_t r_m_pop(void) {
    i_w = 1;
    uint32_t addr = SEGREG(REG_SS, REG_SP,);
    regs16[REG_SP] += 2;
    op_dest = op_source = (uint32_t)(op_result = mem_read16(addr));
    return (uint16_t)op_result;
}

static uint8_t pc_interrupt(uint8_t interrupt_num) {
    set_opcode(0xCD);
    make_flags();
    r_m_push((uint16_t)scratch_uint);
    r_m_push(regs16[REG_CS]);
    r_m_push(reg_ip);
    regs16[REG_CS] = mem_read16(4 * (uint32_t)interrupt_num + 2);
    reg_ip         = mem_read16(4 * (uint32_t)interrupt_num);
    return regs8[FLAG_TF] = regs8[FLAG_IF] = 0;
}

static int AAA_AAS(int8_t which_operation) {
    regs16[REG_AX] += 262 * which_operation * set_AF(set_CF(((regs8[REG_AL] & 0x0F) > 9) || regs8[FLAG_AF]));
    regs8[REG_AL] &= 0x0F;
    return regs8[REG_AL];
}

#define DECODE_RM_REG \
    scratch2_uint = 4 * !i_mod, \
    op_to_addr = rm_addr = i_mod < 3 ? \
        SEGREG(seg_override_en ? seg_override : bios_table_lookup[scratch2_uint + 3][i_rm], \
               bios_table_lookup[scratch2_uint][i_rm], \
               regs16[bios_table_lookup[scratch2_uint + 1][i_rm]] + bios_table_lookup[scratch2_uint + 2][i_rm] * i_data1 +) \
        : GET_REG_ADDR(i_rm), \
    op_from_addr = GET_REG_ADDR(i_reg), \
    i_d && (scratch_uint = op_from_addr, op_from_addr = rm_addr, op_to_addr = scratch_uint)

#define MUL_MACRO(op_data_type, out_regs) do { \
    set_opcode(0x10); \
    op_result = (op_data_type)m_read(rm_addr) * (op_data_type)(*out_regs); \
    out_regs[i_w + 1] = (uint16_t)((uint32_t)op_result >> 16); \
    regs16[REG_AX] = (uint16_t)op_result; \
    set_OF(set_CF(op_result - (op_data_type)op_result)); \
} while (0)

#define DIV_MACRO(out_data_type, in_data_type, out_regs) do { \
    scratch_int = (out_data_type)m_read(rm_addr); \
    if (scratch_int && !(scratch2_uint = (uint32_t)((in_data_type)(scratch_uint = ((uint32_t)out_regs[i_w + 1] << 16) + regs16[REG_AX]) / scratch_int), scratch2_uint - (uint32_t)(out_data_type)scratch2_uint)) { \
        out_regs[i_w + 1] = (uint16_t)(scratch_uint - (uint32_t)(scratch_int * (*out_regs = (out_data_type)scratch2_uint))); \
    } else { \
        pc_interrupt(0); \
    } \
} while (0)

WASM_EXPORT("emu_reset")
void emu_reset(int warm) {
    if (!warm) {
        for (uint32_t i = 0; i < MEM_ARRAY_SIZE; i++) mem[i] = 0;
        for (uint32_t i = 0; i < VRAM_SIZE; i++) vram[i] = 0;
    } else {
        for (uint32_t i = REGS_BASE; i < MEM_ARRAY_SIZE; i++) mem[i] = 0;
        /* Set warm boot flag at 0040:0072 = 0x1234 */
        mem[0x472] = 0x34;
        mem[0x473] = 0x12;
    }

    regs8  = mem + REGS_BASE;
    regs16 = (uint16_t *)(mem + REGS_BASE);

    /* 8088 Reset Vector: FFFF:0000 */
    regs16[REG_CS] = 0xFFFF;
    reg_ip         = 0x0000;
    regs16[REG_DS] = 0x0000;
    regs16[REG_ES] = 0x0000;
    regs16[REG_SS] = 0x0000;
    regs8[FLAG_TF] = 0;
    regs8[FLAG_IF] = 0;
    regs8[FLAG_DF] = 0;

    seg_override_en = 0;
    rep_override_en = 0;
    trap_flag       = 0;
    inst_counter    = 0;

    crt_ctrl_reg     = 0;
    vram_map_reg     = 0;
    status_3da_first = 1;
    status_3da_cycle = 0;

    pic_irr = 0;
    pic_isr = 0;
    pic_imr = 0xFF;
    pic_icw_step = 0;
    pic_vector_base = 0x08;

    port_61 = 0x20;
    port_c8 = 0x00;
    dma_flipflop = 0;
    dma_mask = 0x0F;

    fdc_cmd_len = 0;
    fdc_res_len = 0;
    fdc_res_pos = 0;
    fdc_int_pending = 0;
    fdc_irq_delay = 0;

    kbd_q_head = 0;
    kbd_q_tail = 0;
    kbd_latch = 0xAA;
    kbd_last_out = 0xFF;
    kbd_waiting_ack = 0;
    kbd_irq_delay = 0;
}

WASM_EXPORT("emu_init")
void emu_init(int bios_version, int ram_kb) {
    active_bios_rom = (bios_version == 1) ? hyperion_bios_c1_rom : hyperion_bios_c7_rom;
    if (ram_kb >= 640) ram_limit = 0xA0000;
    else if (ram_kb <= 256) ram_limit = 0x40000;
    else ram_limit = (uint32_t)ram_kb * 1024;
    emu_reset(0);
}

WASM_EXPORT("emu_set_rtc")
void emu_set_rtc(int year, int month, int day, int dow, int hour, int min, int sec) {
    int y = year % 100;
    rtc_regs[0]  = (uint8_t)(sec % 10);
    rtc_regs[1]  = (uint8_t)((sec / 10) & 7);
    rtc_regs[2]  = (uint8_t)(min % 10);
    rtc_regs[3]  = (uint8_t)((min / 10) & 7);
    rtc_regs[4]  = (uint8_t)(hour % 10);
    rtc_regs[5]  = (uint8_t)(8 | ((hour / 10) & 3)); /* 24-hour mode bit 3 = 1 */
    rtc_regs[6]  = (uint8_t)(dow & 7);
    rtc_regs[7]  = (uint8_t)(day % 10);
    rtc_regs[8]  = (uint8_t)((day / 10) & 3);
    rtc_regs[9]  = (uint8_t)(month % 10);
    rtc_regs[10] = (uint8_t)((month / 10) & 1);
    rtc_regs[11] = (uint8_t)(y % 10);
    rtc_regs[12] = (uint8_t)((y / 10) & 0x0F);
}

WASM_EXPORT("emu_step")
int emu_step(int instructions) {
    uint8_t opcode_stream[6];

    for (int step = 0; step < instructions; step++) {
        uint32_t pc = (16 * (uint32_t)regs16[REG_CS] + reg_ip) & 0xFFFFF;
        for (int k = 0; k < 6; k++) {
            opcode_stream[k] = mem_read8(pc + k);
        }

        set_opcode(opcode_stream[0]);

        i_reg4bit = raw_opcode_id & 7;
        i_w       = i_reg4bit & 1;
        i_d       = (i_reg4bit / 2) & 1;

        i_data0 = (int16_t)(opcode_stream[1] | ((uint16_t)opcode_stream[2] << 8));
        i_data1 = (int16_t)(opcode_stream[2] | ((uint16_t)opcode_stream[3] << 8));
        i_data2 = (int16_t)(opcode_stream[3] | ((uint16_t)opcode_stream[4] << 8));

        if (seg_override_en) seg_override_en--;
        if (rep_override_en) rep_override_en--;

        if (i_mod_size) {
            i_mod = (i_data0 & 0xFF) >> 6;
            i_rm  = i_data0 & 7;
            i_reg = (i_data0 / 8) & 7;

            if ((!i_mod && i_rm == 6) || (i_mod == 2))
                i_data2 = (int16_t)(opcode_stream[4] | ((uint16_t)opcode_stream[5] << 8));
            else if (i_mod != 1)
                i_data2 = i_data1;
            else
                i_data1 = (int8_t)i_data1;

            DECODE_RM_REG;
        }

        switch (xlat_opcode_id) {
            case 0: /* Conditional jumps */
                scratch_uchar = (raw_opcode_id / 2) & 7;
                reg_ip += (int8_t)i_data0 * (i_w ^ (regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_A][scratch_uchar]] ||
                                                    regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_B][scratch_uchar]] ||
                                                   (regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_C][scratch_uchar]] ^
                                                    regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_D][scratch_uchar]])));
                break;

            case 1: /* MOV reg, imm */
                i_w = !!(raw_opcode_id & 8);
                m_op(GET_REG_ADDR(i_reg4bit), '=', i_data0);
                break;

            case 3: /* PUSH regs16 */
                r_m_push(regs16[i_reg4bit]);
                break;

            case 4: /* POP regs16 */
                regs16[i_reg4bit] = r_m_pop();
                break;

            case 2: /* INC|DEC regs16 */
                i_w = 1;
                i_d = 0;
                i_reg = i_reg4bit;
                DECODE_RM_REG;
                i_reg = extra;
                /* fallthrough */
            case 5: /* INC|DEC|JMP|CALL|PUSH */
                if (i_reg < 2) {
                    m_op(op_from_addr, i_reg ? '-' : '+', 1);
                    op_source = 1;
                    set_AF_OF_arith();
                    set_OF(op_dest + 1 - i_reg == (1U << (TOP_BIT - 1)));
                    if (xlat_opcode_id == 5) set_opcode(0x10);
                } else if (i_reg != 6) {
                    uint16_t new_ip = mem_read16(op_from_addr);
                    uint16_t new_cs = (i_reg & 1) ? mem_read16(op_from_addr + 2) : regs16[REG_CS];
                    uint16_t ret_ip = reg_ip + 2 + i_mod * (i_mod != 3) + 2 * (!i_mod && i_rm == 6);
                    if (i_reg == 3) r_m_push(regs16[REG_CS]);
                    if (i_reg & 2)  r_m_push(ret_ip);
                    if (i_reg & 1)  regs16[REG_CS] = new_cs;
                    reg_ip = new_ip;
                    set_opcode(0x9A);
                } else {
                    r_m_push(mem_read16(rm_addr));
                }
                break;

            case 6: /* TEST r/m, imm16 / NOT|NEG|MUL|IMUL|DIV|IDIV reg */
                op_to_addr = op_from_addr;
                switch (i_reg) {
                    case 0: /* TEST */
                    case 1:
                        set_opcode(0x20);
                        reg_ip += i_w + 1;
                        m_op(op_to_addr, 't', i_data2);
                        break;
                    case 2: /* NOT */
                        OP('~');
                        break;
                    case 3: /* NEG */
                        OP('n');
                        op_dest = 0;
                        set_opcode(0x28);
                        set_CF((uint32_t)op_result > op_dest);
                        break;
                    case 4: /* MUL */
                        if (i_w) MUL_MACRO(uint16_t, regs16);
                        else     MUL_MACRO(uint8_t, regs8);
                        break;
                    case 5: /* IMUL */
                        if (i_w) MUL_MACRO(int16_t, regs16);
                        else     MUL_MACRO(int8_t, regs8);
                        break;
                    case 6: /* DIV */
                        if (i_w) DIV_MACRO(uint16_t, uint32_t, regs16);
                        else     DIV_MACRO(uint8_t, uint16_t, regs8);
                        break;
                    case 7: /* IDIV */
                        if (i_w) DIV_MACRO(int16_t, int32_t, regs16);
                        else     DIV_MACRO(int8_t, int16_t, regs8);
                        break;
                }
                break;

            case 7: /* ADD|OR|ADC|SBB|AND|SUB|XOR|CMP AL/AX, immed */
                rm_addr = REGS_BASE;
                i_data2 = i_data0;
                i_mod = 3;
                i_reg = extra;
                reg_ip--;
                /* fallthrough */
            case 8: /* ADD|OR|ADC|SBB|AND|SUB|XOR|CMP r/m, immed */
                op_to_addr = rm_addr;
                regs16[REG_SCRATCH] = (i_d |= !i_w) ? (int8_t)i_data2 : i_data2;
                op_from_addr = REGS_BASE + 2 * REG_SCRATCH;
                reg_ip += !i_d + 1;
                set_opcode(0x08 * (extra = i_reg));
                /* fallthrough */
            case 9: /* ADD|OR|ADC|SBB|AND|SUB|XOR|CMP|MOV reg, r/m */
                switch (extra) {
                    case 0: /* ADD */
                        OP('+');
                        set_CF((uint32_t)op_result < op_dest);
                        break;
                    case 1: /* OR */
                        OP('|');
                        break;
                    case 2: /* ADC */
                        OP('A');
                        set_CF((regs8[FLAG_CF] && ((uint32_t)op_result == op_dest)) || ((uint32_t)op_result < op_dest));
                        set_AF_OF_arith();
                        break;
                    case 3: /* SBB */
                        OP('S');
                        set_CF((regs8[FLAG_CF] && ((uint32_t)op_result == op_dest)) || ((uint32_t)op_result > op_dest));
                        set_AF_OF_arith();
                        break;
                    case 4: /* AND */
                        OP('&');
                        break;
                    case 5: /* SUB */
                        OP('-');
                        set_CF((uint32_t)op_result > op_dest);
                        break;
                    case 6: /* XOR */
                        OP('^');
                        break;
                    case 7: /* CMP */
                        OP('c');
                        set_CF((uint32_t)op_result > op_dest);
                        break;
                    case 8: /* MOV */
                        OP('=');
                        break;
                }
                break;

            case 10: /* MOV sreg, r/m | POP r/m | LEA reg, r/m */
                if (!i_w) {
                    i_w = 1;
                    i_reg += 8;
                    DECODE_RM_REG;
                    OP('=');
                } else if (!i_d) {
                    seg_override_en = 1;
                    seg_override = REG_ZERO;
                    DECODE_RM_REG;
                    m_op(op_from_addr, '=', rm_addr);
                } else {
                    uint16_t v = r_m_pop();
                    m_op(rm_addr, '=', v);
                }
                break;

            case 11: /* MOV AL/AX, [loc] */
                i_mod = i_reg = 0;
                i_rm = 6;
                i_data1 = i_data0;
                DECODE_RM_REG;
                MEM_OP(op_from_addr, '=', op_to_addr);
                break;

            case 12: /* ROL|ROR|RCL|RCR|SHL|SHR|SAR */
                scratch2_uint = SIGN_OF(m_read(rm_addr));
                if (extra) {
                    ++reg_ip;
                    scratch_uint = (int8_t)i_data1;
                } else if (i_d) {
                    scratch_uint = 31 & regs8[REG_CL];
                } else {
                    scratch_uint = 1;
                }
                if (scratch_uint) {
                    if (i_reg < 4) {
                        scratch_uint %= (i_reg / 2) + TOP_BIT;
                        scratch2_uint = m_read(rm_addr);
                    }
                    if (i_reg & 1)
                        m_op(rm_addr, '>', scratch_uint);
                    else
                        m_op(rm_addr, '<', scratch_uint);
                    if (i_reg > 3)
                        set_opcode(0x10);
                    if (i_reg > 4)
                        set_CF((op_dest >> (scratch_uint - 1)) & 1);
                }
                switch (i_reg) {
                    case 0: /* ROL */
                        if (scratch_uint) m_op(rm_addr, '+', scratch2_uint >> (TOP_BIT - scratch_uint));
                        set_OF(SIGN_OF(op_result) ^ set_CF(op_result & 1));
                        break;
                    case 1: /* ROR */
                        if (scratch_uint) {
                            scratch2_uint &= (1U << scratch_uint) - 1;
                            m_op(rm_addr, '+', scratch2_uint << (TOP_BIT - scratch_uint));
                        }
                        set_OF(SIGN_OF(op_result * 2) ^ set_CF(SIGN_OF(op_result)));
                        break;
                    case 2: /* RCL */
                        if (scratch_uint) {
                            m_op(rm_addr, '+', (regs8[FLAG_CF] << (scratch_uint - 1)) + (scratch2_uint >> (1 + TOP_BIT - scratch_uint)));
                            set_OF(SIGN_OF(op_result) ^ set_CF(scratch2_uint & (1U << (TOP_BIT - scratch_uint))));
                        }
                        break;
                    case 3: /* RCR */
                        if (scratch_uint) {
                            m_op(rm_addr, '+', (regs8[FLAG_CF] << (TOP_BIT - scratch_uint)) + (scratch2_uint << (1 + TOP_BIT - scratch_uint)));
                            set_CF(scratch2_uint & (1U << (scratch_uint - 1)));
                            set_OF(SIGN_OF(op_result) ^ SIGN_OF(op_result * 2));
                        }
                        break;
                    case 4: /* SHL */
                        if (scratch_uint) set_OF(SIGN_OF(op_result) ^ set_CF(SIGN_OF(op_dest << (scratch_uint - 1))));
                        break;
                    case 5: /* SHR */
                        if (scratch_uint) set_OF(SIGN_OF(op_dest));
                        break;
                    case 7: /* SAR */
                        if (scratch_uint >= TOP_BIT) set_CF(scratch2_uint);
                        set_OF(0);
                        if (scratch_uint) {
                            uint32_t mask = ~(((1U << TOP_BIT) - 1) >> scratch_uint);
                            m_op(rm_addr, '+', scratch2_uint * mask);
                        }
                        break;
                }
                break;

            case 13: /* LOOPxx|JCXZ */
                scratch_uint = !!--regs16[REG_CX];
                switch (i_reg4bit) {
                    case 0: scratch_uint &= !regs8[FLAG_ZF]; break;
                    case 1: scratch_uint &= regs8[FLAG_ZF]; break;
                    case 3: scratch_uint = !++regs16[REG_CX]; break;
                }
                reg_ip += scratch_uint * (int8_t)i_data0;
                break;

            case 14: /* JMP | CALL short/near/far */
                reg_ip += 3 - i_d;
                if (!i_w) {
                    if (i_d) {
                        reg_ip = 0;
                        regs16[REG_CS] = i_data2;
                    } else {
                        r_m_push(reg_ip);
                    }
                }
                reg_ip += (i_d && i_w) ? (int8_t)i_data0 : i_data0;
                break;

            case 15: /* TEST reg, r/m */
                MEM_OP(op_from_addr, 't', op_to_addr);
                break;

            case 16: /* XCHG AX, regs16 */
                i_w = 1;
                op_to_addr = REGS_BASE;
                op_from_addr = GET_REG_ADDR(i_reg4bit);
                /* fallthrough */
            case 24: /* NOP|XCHG reg, r/m */
                if (op_to_addr != op_from_addr) {
                    OP('^');
                    MEM_OP(op_from_addr, '^', op_to_addr);
                    OP('^');
                }
                break;

            case 17: /* MOVSx (extra=0)|STOSx (extra=1)|LODSx (extra=2) */
                scratch2_uint = seg_override_en ? seg_override : REG_DS;
                for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1; scratch_uint; scratch_uint--) {
                    MEM_OP(extra < 2 ? SEGREG(REG_ES, REG_DI,) : REGS_BASE,
                           '=',
                           (extra & 1) ? REGS_BASE : SEGREG(scratch2_uint, REG_SI,));
                    if (!(extra & 1)) INDEX_INC(REG_SI);
                    if (!(extra & 2)) INDEX_INC(REG_DI);
                }
                if (rep_override_en)
                    regs16[REG_CX] = 0;
                break;

            case 18: /* CMPSx (extra=0)|SCASx (extra=1) */
                scratch2_uint = seg_override_en ? seg_override : REG_DS;
                if ((scratch_uint = rep_override_en ? regs16[REG_CX] : 1)) {
                    for (; scratch_uint; rep_override_en || scratch_uint--) {
                        MEM_OP(extra ? REGS_BASE : SEGREG(scratch2_uint, REG_SI,),
                               'c',
                               SEGREG(REG_ES, REG_DI,));
                        if (!extra) INDEX_INC(REG_SI);
                        INDEX_INC(REG_DI);
                        if (rep_override_en && !(--regs16[REG_CX] && ((!op_result) == rep_mode)))
                            scratch_uint = 0;
                    }
                    set_flags_type = FLAGS_UPDATE_SZP | FLAGS_UPDATE_AO_ARITH;
                    set_CF((uint32_t)op_result > op_dest);
                }
                break;

            case 19: /* RET|RETF|IRET */
                i_d = i_w;
                reg_ip = r_m_pop();
                if (extra)
                    regs16[REG_CS] = r_m_pop();
                if (extra & 2)
                    set_flags(r_m_pop());
                else if (!i_d)
                    regs16[REG_SP] += i_data0;
                break;

            case 20: /* MOV r/m, immed */
                m_op(op_from_addr, '=', i_data2);
                break;

            case 21: /* IN AL/AX, DX/imm8 */
                scratch_uint = extra ? regs16[REG_DX] : (uint8_t)i_data0;
                regs8[REG_AL] = io_read8((uint16_t)scratch_uint);
                if (i_w) regs8[REG_AH] = io_read8((uint16_t)(scratch_uint + 1));
                break;

            case 22: /* OUT DX/imm8, AL/AX */
                scratch_uint = extra ? regs16[REG_DX] : (uint8_t)i_data0;
                io_write8((uint16_t)scratch_uint, regs8[REG_AL]);
                if (i_w) io_write8((uint16_t)(scratch_uint + 1), regs8[REG_AH]);
                break;

            case 23: /* REPxx */
                rep_override_en = 2;
                rep_mode = i_w;
                if (seg_override_en) seg_override_en++;
                break;

            case 25: /* PUSH sreg */
                r_m_push(regs16[extra]);
                break;

            case 26: /* POP sreg */
                regs16[extra] = r_m_pop();
                break;

            case 27: /* Segment overrides */
                seg_override_en = 2;
                seg_override = extra;
                if (rep_override_en) rep_override_en++;
                break;

            case 28: /* DAA/DAS */
                i_w = 0;
                if (extra) {
                    /* DAS */
                    if (set_AF((((scratch2_uint = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF])) {
                        op_result = (regs8[REG_AL] -= 6);
                        set_CF(regs8[FLAG_CF] || (regs8[REG_AL] >= scratch2_uint));
                    }
                    if (set_CF(((scratch2_uint & 0xFF) > 0x99) || regs8[FLAG_CF])) {
                        op_result = (regs8[REG_AL] -= 0x60);
                    }
                } else {
                    /* DAA */
                    if (set_AF((((scratch2_uint = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF])) {
                        op_result = (regs8[REG_AL] += 6);
                        set_CF(regs8[FLAG_CF] || (regs8[REG_AL] < scratch2_uint));
                    }
                    if (set_CF(((regs8[REG_AL] & 0xF0) > 0x90) || regs8[FLAG_CF])) {
                        op_result = (regs8[REG_AL] += 0x60);
                    }
                }
                break;

            case 29: /* AAA/AAS */
                op_result = AAA_AAS((int8_t)(extra - 1));
                break;

            case 30: /* CBW */
                regs8[REG_AH] = (uint8_t)(-SIGN_OF(regs8[REG_AL]));
                break;

            case 31: /* CWD */
                regs16[REG_DX] = (uint16_t)(-SIGN_OF(regs16[REG_AX]));
                break;

            case 32: /* CALL FAR imm16:imm16 */
                r_m_push(regs16[REG_CS]);
                r_m_push(reg_ip + 5);
                regs16[REG_CS] = i_data2;
                reg_ip = i_data0;
                break;

            case 33: /* PUSHF */
                make_flags();
                r_m_push((uint16_t)scratch_uint);
                break;

            case 34: /* POPF */
                set_flags(r_m_pop());
                break;

            case 35: /* SAHF */
                make_flags();
                set_flags((scratch_uint & 0xFF00) + regs8[REG_AH]);
                break;

            case 36: /* LAHF */
                make_flags();
                regs8[REG_AH] = (uint8_t)scratch_uint;
                break;

            case 37: /* LES|LDS reg, r/m */
                i_w = i_d = 1;
                DECODE_RM_REG;
                OP('=');
                MEM_OP(REGS_BASE + extra, '=', rm_addr + 2);
                break;

            case 38: /* INT 3 */
                ++reg_ip;
                pc_interrupt(3);
                break;

            case 39: /* INT imm8 */
                reg_ip += 2;
                pc_interrupt((uint8_t)i_data0);
                break;

            case 40: /* INTO */
                ++reg_ip;
                if (regs8[FLAG_OF]) pc_interrupt(4);
                break;

            case 41: /* AAM */
                if ((i_data0 &= 0xFF)) {
                    regs8[REG_AH] = regs8[REG_AL] / (uint8_t)i_data0;
                    op_result = (regs8[REG_AL] %= (uint8_t)i_data0);
                } else {
                    pc_interrupt(0);
                }
                break;

            case 42: /* AAD */
                i_w = 0;
                regs16[REG_AX] = op_result = 0xFF & (regs8[REG_AL] + (uint8_t)i_data0 * regs8[REG_AH]);
                break;

            case 43: /* SALC */
                regs8[REG_AL] = (uint8_t)(-regs8[FLAG_CF]);
                break;

            case 44: /* XLAT */
                regs8[REG_AL] = mem_read8(SEGREG(seg_override_en ? seg_override : REG_DS, REG_BX, regs8[REG_AL] +));
                break;

            case 45: /* CMC */
                regs8[FLAG_CF] ^= 1;
                break;

            case 46: /* CLC|STC|CLI|STI|CLD|STD */
                regs8[extra / 2] = extra & 1;
                break;

            case 47: /* TEST AL/AX, immed */
                m_op(REGS_BASE + REG_AL, 't', i_data0);
                break;
        }

        reg_ip += (uint16_t)((i_mod * (i_mod != 3) + 2 * (!i_mod && i_rm == 6)) * i_mod_size +
                             bios_table_lookup[TABLE_BASE_INST_SIZE][raw_opcode_id] +
                             bios_table_lookup[TABLE_I_W_SIZE][raw_opcode_id] * (i_w + 1));

        if (set_flags_type & FLAGS_UPDATE_SZP) {
            regs8[FLAG_SF] = SIGN_OF(op_result);
            regs8[FLAG_ZF] = !op_result;
            regs8[FLAG_PF] = bios_table_lookup[TABLE_PARITY_FLAG][(uint8_t)op_result];

            if (set_flags_type & FLAGS_UPDATE_AO_ARITH)
                set_AF_OF_arith();
            if (set_flags_type & FLAGS_UPDATE_OC_LOGIC) {
                set_CF(0);
                set_OF(0);
            }
        }

        inst_counter++;

        /* Hardware Timers & Deferred Interrupts */
        if (fdc_irq_delay > 0) {
            if (--fdc_irq_delay == 0) {
                pic_request_irq(6); /* IRQ6: 8272A FDC */
            }
        }

        if (kbd_irq_delay > 0) {
            kbd_irq_delay--;
        }
        if (!kbd_waiting_ack && kbd_irq_delay == 0 && kbd_q_head != kbd_q_tail) {
            kbd_latch = kbd_queue[kbd_q_head];
            kbd_q_head = (kbd_q_head + 1) & (KBD_QUEUE_SIZE - 1);
            kbd_waiting_ack = 1;
            pic_request_irq(1); /* IRQ1: Keyboard */
        }

        /* IRQ2: CRT Vertical Retrace (60 Hz -> ~8,000 instructions) */
        if ((inst_counter & 0x1FFF) == 0) {
            pic_request_irq(2);
        }

        /* IRQ0: 8253 PIT Channel 0 Timer (18.2 Hz -> ~26,000 instructions) */
        if ((inst_counter % 26000) == 0) {
            pic_request_irq(0);
        }

        if (trap_flag)
            pc_interrupt(1);
        trap_flag = regs8[FLAG_TF];

        /* Service highest-priority unmasked PIC interrupt if IF=1 */
        if (!seg_override_en && !rep_override_en && regs8[FLAG_IF] && !regs8[FLAG_TF]) {
            uint8_t pending = pic_irr & (uint8_t)(~pic_imr);
            if (pending) {
                for (uint8_t irq = 0; irq < 8; irq++) {
                    if (pending & (1 << irq)) {
                        pic_irr &= (uint8_t)(~(1 << irq));
                        pic_isr |= (uint8_t)(1 << irq);
                        pc_interrupt(pic_vector_base + irq);
                        break;
                    }
                }
            }
        }
    }

    return instructions;
}

/* Video Rendering (7-inch Amber Phosphor CRT, 640 x 250) */
/* Amber color palette (0xFFBBGGRR in little-endian RGBA) */
static const uint32_t amber_palette[4] = {
    0xFF0A0808, /* 0: CRT Dark Amber/Black Phosphor Background */
    0xFF0055AA, /* 1: Faint Amber */
    0xFF0088EE, /* 2: Normal Bright Amber (#EE8800) */
    0xFF38C8FF  /* 3: Intensified / Bold Amber (#FFC838) */
};

static void render_display(void) {
    blink_counter++;
    int blink_on    = (blink_counter & 16) != 0;
    int cursor_on   = (blink_counter & 8) != 0;

    for (int d = 0; d < FDC_MAX_DRIVES; d++) {
        if (drive_led_timer[d] > 0) drive_led_timer[d]--;
    }

    uint8_t mode = mem[0x449]; /* BDA 0040:0049 current video mode */

    /* Native Hyperion 640x250 High-Res or 320x250 Medium-Res Graphics (modes 0x86 / 0x84 / 0x85) */
    if (mode == 0x86) {
        /* 640x250 linear 1-bpp graphics (20,000 bytes) */
        for (int y = 0; y < 250; y++) {
            uint32_t *row_out = &framebuffer[y * HYPERION_SCREEN_WIDTH];
            int row_off = y * 80;
            for (int bx = 0; bx < 80; bx++) {
                uint8_t b = vram[row_off + bx];
                for (int bit = 0; bit < 8; bit++) {
                    row_out[bx * 8 + bit] = (b & (0x80 >> bit)) ? amber_palette[2] : amber_palette[0];
                }
            }
        }
        return;
    }

    if (mode == 0x84 || mode == 0x85) {
        /* 320x250 linear 2-bpp 4-shade amber graphics (20,000 bytes) */
        for (int y = 0; y < 250; y++) {
            uint32_t *row_out = &framebuffer[y * HYPERION_SCREEN_WIDTH];
            int row_off = y * 80;
            for (int bx = 0; bx < 80; bx++) {
                uint8_t b = vram[row_off + bx];
                for (int p = 0; p < 4; p++) {
                    uint8_t shade = (b >> (6 - p * 2)) & 3;
                    uint32_t col = amber_palette[shade];
                    row_out[bx * 8 + p * 2]     = col;
                    row_out[bx * 8 + p * 2 + 1] = col;
                }
            }
        }
        return;
    }

    /* IBM-compatible 640x200 (mode 6) or 320x200 (modes 4, 5) CGA Graphics */
    if (mode == 4 || mode == 5 || mode == 6) {
        for (int i = 0; i < HYPERION_SCREEN_WIDTH * HYPERION_SCREEN_HEIGHT; i++) {
            framebuffer[i] = amber_palette[0];
        }
        int y_pad = 25; /* Center 200 scanlines vertically in 250-line CRT */
        for (int y = 0; y < 200; y++) {
            uint32_t *row_out = &framebuffer[(y + y_pad) * HYPERION_SCREEN_WIDTH];
            int vram_off = ((y & 1) ? 0x2000 : 0x0000) + (y >> 1) * 80;
            if (mode == 6) {
                for (int bx = 0; bx < 80; bx++) {
                    uint8_t b = vram[vram_off + bx];
                    for (int bit = 0; bit < 8; bit++) {
                        row_out[bx * 8 + bit] = (b & (0x80 >> bit)) ? amber_palette[2] : amber_palette[0];
                    }
                }
            } else {
                for (int bx = 0; bx < 80; bx++) {
                    uint8_t b = vram[vram_off + bx];
                    for (int p = 0; p < 4; p++) {
                        uint8_t shade = (b >> (6 - p * 2)) & 3;
                        uint32_t col = amber_palette[shade];
                        row_out[bx * 8 + p * 2]     = col;
                        row_out[bx * 8 + p * 2 + 1] = col;
                    }
                }
            }
        }
        return;
    }

    /* Alphanumeric Text Mode (80x25 or 40x25, 8x10 character cells = 640x250) */
    int cols = (mode == 0 || mode == 1 || mode == 0x80 || mode == 0x81) ? 40 : 80;
    int is_hyperion_attr = (mode >= 0x80) || ((mem[0x410] & 0x30) == 0x30);
    uint16_t page_offset = (uint16_t)(mem[0x44E] | ((uint16_t)mem[0x44F] << 8));
    uint8_t active_page  = mem[0x462] & 7;
    uint8_t cur_col      = mem[0x450 + active_page * 2];
    uint8_t cur_row      = mem[0x450 + active_page * 2 + 1];
    uint8_t cur_end      = mem[0x460] & 0x1F;
    uint8_t cur_start    = mem[0x461] & 0x1F;
    int cur_visible      = (mem[0x461] & 0x20) == 0 && (cur_start <= cur_end);
    if ((crt_ctrl_reg & 0xC0) == 0x00 && crt_ctrl_reg != 0) {
        cur_visible = 0;
    }

    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < cols; col++) {
            uint32_t cell_addr = (page_offset + (row * cols + col) * 2) % 0x5000;
            uint8_t ch   = vram[cell_addr];
            uint8_t attr = vram[cell_addr + 1];

            int fg_idx = 2; /* Normal amber */
            int bg_idx = 0; /* Dark background */
            int underline = 0;
            int rev_video = 0;
            int blink     = 0;
            int blank     = 0;
            int sup       = 0;
            int sub       = 0;
            int dbl_left  = 0;
            int dbl_right = 0;

            if (is_hyperion_attr) {
                /* Table II-O: Hyperion character attribute byte */
                underline = (attr & 0x01) != 0;
                rev_video = (attr & 0x02) != 0;
                blink     = (attr & 0x04) != 0;
                sup       = (attr & 0x08) != 0;
                sub       = (attr & 0x10) != 0;
                if (attr & 0x20) fg_idx = 3; /* Intensify (bold) */
                dbl_left  = (attr & 0xC0) == 0x40;
                dbl_right = (attr & 0xC0) == 0x80;
            } else {
                /* Table II-N: IBM-compatible character attribute byte */
                uint8_t fg = attr & 0x07;
                uint8_t bg = (attr >> 4) & 0x07;
                if (fg == 0 && bg == 0) {
                    blank = 1;
                } else if (fg == 7 && bg == 7) {
                    rev_video = 1;
                    blank = 1;
                } else if (bg == 7 && fg == 0) {
                    rev_video = 1;
                } else if ((attr & 0x07) == 0x01) {
                    underline = 1;
                }
                if (attr & 0x08) fg_idx = 3;
                if (attr & 0x80) blink = 1;
            }

            if (rev_video) {
                bg_idx = (fg_idx == 3) ? 3 : 2;
                fg_idx = 0;
            }
            if (blink && !blink_on) {
                fg_idx = bg_idx;
            }
            if (blank) {
                fg_idx = bg_idx;
            }

            uint32_t fg_color = amber_palette[fg_idx];
            uint32_t bg_color = amber_palette[bg_idx];
            const uint8_t *glyph = &hyperion_char_rom[(uint32_t)ch * 16];

            int is_cursor_cell = cur_visible && cursor_on && (row == cur_row) && (col == cur_col);
            int c_start = (cur_start < 10) ? cur_start : 8;
            int c_end   = (cur_end < 10)   ? cur_end   : 9;

            for (int sy = 0; sy < 10; sy++) {
                int glyph_row = sy;
                if (sup && !sub) glyph_row = sy + 2;
                else if (sub && !sup) glyph_row = (sy >= 2) ? (sy - 2) : 15;
                uint8_t raw_bits = (glyph_row >= 0 && glyph_row < 16) ? glyph[glyph_row] : 0;
                uint8_t bits = raw_bits;
                if (dbl_left) {
                    bits = 0;
                    for (int b = 0; b < 4; b++) {
                        if (raw_bits & (0x80 >> b)) bits |= (0xC0 >> (b * 2));
                    }
                } else if (dbl_right) {
                    bits = 0;
                    for (int b = 0; b < 4; b++) {
                        if (raw_bits & (0x08 >> b)) bits |= (0xC0 >> (b * 2));
                    }
                }

                if (underline && sy == 9) {
                    bits = 0xFF;
                }
                if (is_cursor_cell && sy >= c_start && sy <= c_end) {
                    bits ^= 0xFF;
                }

                uint32_t *dst = &framebuffer[(row * 10 + sy) * HYPERION_SCREEN_WIDTH + col * (cols == 40 ? 16 : 8)];
                if (cols == 40) {
                    for (int sx = 0; sx < 8; sx++) {
                        uint32_t pix = (bits & (0x80 >> sx)) ? fg_color : bg_color;
                        dst[sx * 2]     = pix;
                        dst[sx * 2 + 1] = pix;
                    }
                } else {
                    for (int sx = 0; sx < 8; sx++) {
                        dst[sx] = (bits & (0x80 >> sx)) ? fg_color : bg_color;
                    }
                }
            }
        }
    }
}

WASM_EXPORT("emu_run_frame")
int emu_run_frame(void) {
    emu_step(45000);
    render_display();
    return 45000;
}

/* Disk Image Loader (Supports both ImageDisk .IMD and raw .IMG/.DSK) */
WASM_EXPORT("emu_load_disk")
int emu_load_disk(int drive, const uint8_t *data, int len) {
    if (drive < 0 || drive >= FDC_MAX_DRIVES || !data || len <= 0)
        return -1;

    for (int c = 0; c < FDC_MAX_CYLS; c++)
        for (int h = 0; h < FDC_MAX_HEADS; h++)
            for (int s = 0; s < FDC_MAX_SECTORS; s++)
                for (int b = 0; b < FDC_SECTOR_SIZE; b++)
                    floppy_data[drive][c][h][s][b] = 0;

    /* Check for ImageDisk (.IMD) header "IMD " */
    if (len > 32 && data[0] == 'I' && data[1] == 'M' && data[2] == 'D') {
        int pos = 0;
        while (pos < len && data[pos] != 0x1A) pos++;
        if (pos >= len) return -2;
        pos++; /* Skip 0x1A EOF marker */

        uint8_t max_spt = 8;
        int tracks_loaded = 0;
        while (pos + 5 <= len) {
            uint8_t cyl        = data[pos + 1];
            uint8_t head_flags = data[pos + 2];
            uint8_t nsec       = data[pos + 3];
            uint8_t sz_code    = data[pos + 4];
            pos += 5;

            uint8_t head = head_flags & 0x0F;
            int sec_size = 128 << sz_code;
            if (pos + nsec > len) break;

            const uint8_t *smap = &data[pos];
            pos += nsec;
            if (head_flags & 0x80) pos += nsec; /* Cylinder map */
            if (head_flags & 0x40) pos += nsec; /* Head map */

            if (nsec > max_spt && nsec <= FDC_MAX_SECTORS)
                max_spt = nsec;

            for (int i = 0; i < nsec; i++) {
                if (pos >= len) break;
                uint8_t stype = data[pos++];
                uint8_t s_num = smap[i];
                uint8_t *dst = (cyl < FDC_MAX_CYLS && head < FDC_MAX_HEADS &&
                                s_num >= 1 && s_num <= FDC_MAX_SECTORS && sec_size == FDC_SECTOR_SIZE)
                               ? floppy_data[drive][cyl][head][s_num - 1]
                               : 0;

                if (stype == 0) {
                    /* Unavailable */
                } else if (stype == 1 || stype == 3 || stype == 5 || stype == 7) {
                    /* Uncompressed sector data */
                    if (pos + sec_size <= len && dst) {
                        for (int b = 0; b < sec_size; b++) dst[b] = data[pos + b];
                    }
                    pos += sec_size;
                } else if (stype == 2 || stype == 4 || stype == 6 || stype == 8) {
                    /* Compressed sector data */
                    uint8_t fill = (pos < len) ? data[pos++] : 0;
                    if (dst) {
                        for (int b = 0; b < sec_size; b++) dst[b] = fill;
                    }
                }
            }
            tracks_loaded++;
        }

        floppy_spt[drive] = max_spt;
        floppy_mounted[drive] = 1;
        return tracks_loaded;
    }

    /* Raw Sector Image (.IMG / .DSK): 160K, 180K, 320K, 360K */
    int total_sectors = len / FDC_SECTOR_SIZE;
    int heads = (total_sectors <= 400) ? 1 : 2;
    int spt   = (total_sectors % 9 == 0) ? 9 : 8;
    int pos = 0;
    for (int c = 0; c < 40; c++) {
        for (int h = 0; h < heads; h++) {
            for (int s = 0; s < spt; s++) {
                if (pos + FDC_SECTOR_SIZE <= len) {
                    for (int b = 0; b < FDC_SECTOR_SIZE; b++) {
                        floppy_data[drive][c][h][s][b] = data[pos + b];
                    }
                    pos += FDC_SECTOR_SIZE;
                }
            }
        }
    }
    floppy_spt[drive] = (uint8_t)spt;
    floppy_mounted[drive] = 1;
    return 40 * heads;
}

WASM_EXPORT("emu_eject_disk")
void emu_eject_disk(int drive) {
    if (drive >= 0 && drive < FDC_MAX_DRIVES) {
        floppy_mounted[drive] = 0;
    }
}

/* Keyboard Input: Hyperion native keycode, IBM XT scancode, or direct ASCII */
WASM_EXPORT("emu_key_event")
void emu_key_event(int hyperion_keycode, int pressed) {
    uint8_t code = (uint8_t)(hyperion_keycode & 0x7F);
    if (!pressed) code |= 0x80;
    kbd_push_code(code);
}

WASM_EXPORT("emu_xt_key_event")
void emu_xt_key_event(int xt_scancode, int pressed) {
    /*
     * Map standard IBM PC XT scan code (0x01 - 0x53) to Hyperion hardware keycode
     * by inverting the BIOS lookup table at FE00:0714!
     */
    uint8_t target = (uint8_t)(xt_scancode & 0x7F);
    for (uint8_t k = 1; k < 0x5E; k++) {
        if (hyperion_bios_c7_rom[0x0714 + k] == target) {
            emu_key_event(k, pressed);
            return;
        }
    }
}

WASM_EXPORT("emu_ascii_keypress")
void emu_ascii_keypress(int ascii) {
    /* Push directly into BIOS keyboard buffer at 0040:001E..003D for automated testing / paste */
    uint16_t head = mem_read16(0x41A);
    uint16_t tail = mem_read16(0x41C);
    if (head < 0x1E || head >= 0x3E || tail < 0x1E || tail >= 0x3E) return;
    uint16_t next = tail + 2;
    if (next >= 0x3E) next = 0x1E;
    if (next != head) {
        uint8_t sc = 0;
        if (ascii == 13) sc = 0x1C;
        else if (ascii == 8) sc = 0x0E;
        else if (ascii == 27) sc = 0x01;
        mem_write16(0x400 + tail, (uint16_t)((ascii & 0xFF) | ((uint16_t)sc << 8)));
        mem_write16(0x41C, next);
    }
}

WASM_EXPORT("emu_get_framebuffer")
const uint32_t* emu_get_framebuffer(void) {
    return framebuffer;
}

WASM_EXPORT("emu_get_vram")
const uint8_t* emu_get_vram(void) {
    return vram;
}

WASM_EXPORT("emu_get_ram")
uint8_t* emu_get_ram(void) {
    return mem;
}

WASM_EXPORT("emu_get_disk_upload_buffer")
uint8_t* emu_get_disk_upload_buffer(int max_size) {
    if (max_size > UPLOAD_BUF_SIZE) return 0;
    return disk_upload_buf;
}

WASM_EXPORT("emu_get_reg")
uint16_t emu_get_reg(int reg_id) {
    if (reg_id >= 0 && reg_id < 12) return regs16[reg_id];
    return 0;
}

WASM_EXPORT("emu_get_ip")
uint16_t emu_get_ip(void) {
    return reg_ip;
}

WASM_EXPORT("emu_get_flags")
uint16_t emu_get_flags(void) {
    make_flags();
    return (uint16_t)scratch_uint;
}

WASM_EXPORT("emu_get_video_mode")
uint8_t emu_get_video_mode(void) {
    return mem[0x449];
}

WASM_EXPORT("emu_get_vram_map")
uint8_t emu_get_vram_map(void) {
    return vram_map_reg;
}

WASM_EXPORT("emu_get_drive_led")
uint8_t emu_get_drive_led(int drive) {
    if (drive < 0 || drive >= FDC_MAX_DRIVES) return 0;
    return (drive_led_timer[drive] > 0) || ((port_c8 & (1 << drive)) != 0 && drive_led_timer[drive] > 0);
}

WASM_EXPORT("emu_get_speaker_freq")
uint32_t emu_get_speaker_freq(void) {
    if ((port_61 & 0x03) != 0x03) return 0;
    uint16_t div = pit_reload[2];
    if (div == 0) return 0;
    return 1193180U / div;
}
