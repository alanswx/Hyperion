# Dynalogic Hyperion (1983) — WebAssembly Emulator

**[Run the Dynalogic Hyperion in your browser (GitHub Pages) &rarr;](https://alanswx.github.io/Hyperion/)**

A cycle/bus-accurate WebAssembly emulator for the **1983 Dynalogic Hyperion**, the iconic Canadian portable IBM PC clone designed in Ottawa by Dynalogic Info-Tech Corporation (industrial design by Hovey-Kelley Design, who went on to design the Apple Macintosh).

It runs the **authentic Hyperion BIOS ROMs** (`U48-700002-000-08-V3.0-C7.bin` and `C1.bin`) and **Character Generator ROM** (`U3-CHAR_ROM-2732A.bin`), booting the original Hyperion `.IMD` / `.IMG` floppy diskettes (`MS-DOS 1.25H`, `MS-DOS 2.11`, `Hyperion Diagnostics + BASICA`, and `Microsoft Multiplan`) directly in the browser.

---

## Architecture & Hardware Emulated

The CPU instruction engine is adapted from **8086tiny** (MIT License) with its 8086 decode tables (`src/decode_tables.h`, with `0x0F` restored to real 8086 `POP CS`) paired with a clean-room implementation of the Dynalogic Hyperion motherboard (`src/hyperion.c`):

| Subsystem | Address / Port Map | Description |
| :--- | :--- | :--- |
| **Intel 8088 CPU** | `00000h–FFFFFh` | 4.77 MHz 8088 CPU with 256 KB standard RAM (creating the `C:` RAM disk under H-DOS 1.25H) or 640 KB expanded RAM |
| **System BIOS ROM (`U48`)** | `FE000h–FFFFFh` | 8 KB 2764 EPROM (`V3.0 Rev C7` and `V3.0 Rev C1`) with ROM stack initialization and hardware diagnostics |
| **Character ROM (`U3`)** | Embedded | 4 KB 2732A EPROM (`256` glyphs × `8×10` dot matrix for 80×25 text on a 640×250 raster) |
| **VRAM Mapping Register** | `A8000h` | Maps the 20 KB physical CRT RAM (`0x5000` bytes) to disabled (`0`), both `B0000h` & `B8000h` (`1`), `B8000h` only (`2`), or `B0000h` only (`3`), returning `0xFF` open-bus on unmapped addresses |
| **Hyperion CRT Controller** | `A0000h`, Ports `0180h–019Fh`, `03B0h–03DFh`, `IRQ2` (`INT 0Ah`) | 7-inch amber phosphor CRT (`640×250`). Supports Hyperion text modes (`80h–83h`) with hardware underline, reverse video, blink, intensify, superscript, subscript, and double-width attributes; Hyperion `320×250` 4-shade amber (`84h/85h`) and `640×250` hi-res (`86h`) linear graphics; and IBM CGA/MDA compatibility modes (`00h–07h`) |
| **NEC µPD765 / Intel 8272A FDC** | Ports `00C0h` (MSR), `00C1h` (FIFO), `00C8h` (Control), `0061h` (Status), `IRQ6` (`INT 0Eh`), DMA Ch 2 | Dual 5.25" 320 KB / 360 KB floppy drives (`A:` and `B:`), natively parsing both ImageDisk (`.IMD`) and raw sector (`.IMG` / `.DSK`) images |
| **Hyperion Keyboard Interface** | Port `00E0h`, `IRQ1` (`INT 09h`) | Inverted hardware strobe/acknowledge port with `00h -> FFh` self-test (`AAh`) and scan code translation via BIOS table `FE00:0714` |
| **8255 PPI + Oki MSM5832 RTC** | Ports `0140h–0143h` | Battery-backed Real-Time Clock bridged via 8255 PPI Ports A & C (used by `DATE` and `CLOCK.SYS` on H-DOS 1.25H / 2.11) |
| **8259A PIC, 8253 PIT, 8237A DMAC, Z80-SIO** | Ports `0000h–0043h`, `0080h–0083h`, `0100h–0103h` | Interrupt controller (`IRQ0` 18.2 Hz timer, `IRQ1` keyboard, `IRQ2` 60 Hz CRT vertical retrace, `IRQ6` FDC), DMA controller, PC speaker, and serial communications |

---

## Quick Start

### 1. Run the WebAssembly Emulator

Start a local web server:

```bash
make serve
```

Then open **[http://localhost:8080/web/](http://localhost:8080/web/)** in your browser.

### 2. Rebuild WebAssembly Module (`web/hyperion.wasm`)

The entire emulator core + embedded BIOS and character ROMs compiles to a standalone **73 KB** WebAssembly binary (`web/hyperion.wasm`) with zero external runtime dependencies:

```bash
make wasm
```

### 3. Run the Native CLI Boot Verification Test

```bash
make test
```

---

## Repository Structure

```text
├── Makefile                # Build & local web server targets
├── roms/                   # Authentic Dynalogic Hyperion BIOS & Character Generator ROMs
│   ├── U3-CHAR_ROM-2732A.bin
│   ├── U48-700002-000-08-V3.0-C1.bin
│   └── U48-700002-000-08-V3.0-C7.bin
├── src/
│   ├── hyperion.h          # Public C / WebAssembly API
│   ├── hyperion.c          # 8088 CPU + Hyperion hardware peripherals & CRT renderer
│   ├── decode_tables.h     # 8086 instruction decode lookup tables (from 8086tiny)
│   └── roms.h              # Embedded ROM byte arrays
├── tests/
│   └── test_boot.c         # Native CLI test harness
├── web/
│   ├── index.html          # Hyperion CRT, F1-F10 soft keys, and dual floppy UI
│   ├── style.css           # Dynalogic Hyperion molded case & amber phosphor styling
│   ├── app.js              # WebAssembly loader, keyboard/audio/disk controller
│   └── hyperion.wasm       # Compiled 73 KB WebAssembly module
└── archive/                # (Git-ignored) Local mirror of vintagecomputer.ca/files/Dynalogic Hyperion/
```

## Credits & License

- Archival ROMs, documentation, and diskette preservation courtesy of **[vintagecomputer.ca](https://vintagecomputer.ca/files/Dynalogic%20Hyperion/)**.
- CPU instruction decoder derived from **[8086tiny](https://github.com/adriancable/8086tiny)** by Adrian Cable (MIT License).
