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
| **ST-506 / ST-412 Hard Disk (C:)** | Ports `0320h–0323h`, `IRQ5` (`INT 0Dh`), BIOS `INT 13h` Drive `80h` | 10 MB (ST-412, 306 cyls) or 20 MB (ST-225, 615 cyls) MFM Hard Disk emulation with Option ROM BIOS hook (`C800:0000`). Supports direct self-booting MS-DOS 2.11 with MBR partition table when floppies are ejected |
| **Hyperion Keyboard Interface** | Port `00E0h`, `IRQ1` (`INT 09h`) | Inverted hardware strobe/acknowledge port with `00h -> FFh` self-test (`AAh`) and scan code translation via BIOS table `FE00:0714` |
| **8255 PPI + Oki MSM5832 RTC** | Ports `0140h–0143h` | Battery-backed Real-Time Clock bridged via 8255 PPI Ports A & C (used by `DATE` and `CLOCK.SYS` on H-DOS 1.25H / 2.11) |
| **8259A PIC, 8253 PIT, 8237A DMAC, Z80-SIO** | Ports `0000h–0043h`, `0080h–0083h`, `0100h–0103h` | Interrupt controller (`IRQ0` 18.2 Hz timer, `IRQ1` keyboard, `IRQ2` 60 Hz CRT vertical retrace, `IRQ6` FDC), DMA controller, PC speaker, and serial communications |

### Options (not Hyperion hardware, or physical switches)

- **Write-protect tab** (`WP` checkbox per drive, `emu_set_write_protect`): Sense Drive Status reports it and writes
  are refused. Infocom booters (Zork I-III, Starcross) refuse to run on a writable disk.
- **PC keyboard port 60h** (`PC Kbd 60h` button, `emu_set_pc_keyboard_port`): IBM PC programs that install their own
  INT 9 handler (Shamus) read the PC's port 60h and acknowledge through port 61h bit 7, which the Hyperion does not
  have; without this their keyboard stops after the first key. Turn it on before starting the program (a reset
  clears a stuck keyboard).

### Disks & Storage

- **Dual Floppy Drives (A: and B:)**: Each drive supports bundled Hyperion diskettes (`web/disks/`) and a **Load .IMD / .IMG** button for custom images (ImageDisk or raw 160/180/320/360 KB); dragging and dropping a diskette file onto the CRT screen automatically mounts it in Drive A: and reboots.
- **Hard Disk Drive (C:)**: Emulates an ST-506 / ST-412 controller at I/O ports `0320h–0323h` with an Option ROM BIOS hook (`C800:0000`). Pre-loaded with `HYP10MB.IMG` containing MS-DOS 2.11, `BASICA.EXE`, utilities, and the `SURF3D` demo.
- **Direct Hard Disk Self-Boot**: When floppies in drives A: and B: are ejected, the BIOS bootloader transfers control to the Hard Disk MBR and boot sector, booting MS-DOS 2.11 directly off `C:`.

### Dynalogic 3D Wireframe Surface Benchmark (`SURF3D`)

The emulator includes the canonical 1983 Dynalogic Hyperion 3D Surface Graphics Benchmark (`SURF3D.BAS`), which showcases high-resolution graphics across 5 simultaneous viewports:
1. **Top-Left**: Hyperbolic paraboloid ("saddle" surface: $z = (x^2 - y^2)/8$)
2. **Top-Right**: Sloping sinusoidal wave sheet ($z = 20 \cos(x/3) \sin(y/3.8) + 1.2y$)
3. **Bottom-Left**: Concentric ripple sombrero ($z = 26 \sin(r/1.2)/(r/2.2)$)
4. **Bottom-Right**: Harmonic ripple trough ($z = 16 \sin(x/2.8) \cos(y/3)$)
5. **Center Inset**: Floating perspective wireframe mesh over a clear mask

**How to run**: Click the amber **"Run 3D Demo (SURF3D)"** button in the web toolbar, or type `SURF3D` at the `C>` prompt. Press any key to exit cleanly back to DOS.

The companion MiSTer FPGA core, which uses this emulator as its reference, is
[Hyperion_MiSTer](https://github.com/alanswx/Hyperion_MiSTer); its `docs/software-tests.md` lists the 1983-84 PC
software tried on both.

---

## Quick Start

### 1. Run the WebAssembly Emulator

Start a local web server:

```bash
make serve
```

Then open **[http://localhost:8080/web/](http://localhost:8080/web/)** in your browser.

### 2. Rebuild WebAssembly Module (`web/hyperion.wasm`)

The entire emulator core + embedded BIOS and character ROMs compiles to a standalone **74 KB** WebAssembly binary (`web/hyperion.wasm`) with zero external runtime dependencies:

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
│   ├── test_boot.c         # Native CLI floppy boot test harness
│   └── test_hd.c           # Hard disk self-boot, MBR partition, and SURF3D regression test
├── web/
│   ├── index.html          # Hyperion CRT, F1-F10 soft keys, and dual floppy UI
│   ├── style.css           # Dynalogic Hyperion molded case & amber phosphor styling
│   ├── app.js              # WebAssembly loader, keyboard/audio/disk controller
│   ├── hyperion.wasm       # Compiled 74 KB WebAssembly module
│   └── disks/              # Bundled floppy and hard disk images (HYP10MB.IMG, etc.)
├── HANDOFF.md              # Project status, architecture notes, and development handoff
└── archive/                # (Git-ignored) Local mirror of vintagecomputer.ca/files/Dynalogic Hyperion/
```

## Credits & License

- Archival ROMs, documentation, and diskette preservation courtesy of **[vintagecomputer.ca](https://vintagecomputer.ca/files/Dynalogic%20Hyperion/)**.
- CPU instruction decoder derived from **[8086tiny](https://github.com/adriancable/8086tiny)** by Adrian Cable (MIT License).
