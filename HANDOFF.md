# Dynalogic Hyperion (1983) Emulator — Project Handoff

**Date**: October 25, 2026  
**Status**: All core features implemented, tested, and deployed to GitHub Pages.  
**Live URL**: [https://alanswx.github.io/Hyperion/](https://alanswx.github.io/Hyperion/)

---

## 1. Executive Summary

The **Dynalogic Hyperion (1983)** is an iconic portable IBM-compatible computer designed in Ottawa, Canada by Dynalogic Info-Tech Corporation (with industrial styling by Hovey-Kelley Design). 

This project provides a standalone, cycle-accurate WebAssembly emulator that compiles into a single **74 KB** `.wasm` binary with zero external runtime dependencies. It boots authentic Hyperion BIOS ROMs (`Rev C7` and `Rev C1`) and character generator ROMs, running native MS-DOS 1.25H and 2.11, diagnostics, productivity software (Multiplan), and the canonical 1983 Dynalogic 3D Wireframe Surface Graphics Benchmark (`SURF3D`).

---

## 2. Hardware Architecture & Peripheral Map

| Subsystem | Address / Port Map | Description |
| :--- | :--- | :--- |
| **Intel 8088 CPU** | `00000h–FFFFFh` | 4.77 MHz 8088 CPU engine derived from 8086tiny with authentic decode tables, full flag handling, and restored `0x0F` (`POP CS`). Configurable 256 KB or 640 KB RAM. |
| **System BIOS ROM (`U48`)** | `FE000h–FFFFFh` | Authentic 8 KB 2764 EPROMs (`V3.0 Rev C7` standard, `V3.0 Rev C1` alternate). Embedded directly in the binary. |
| **Character Generator ROM (`U3`)** | Embedded | Authentic 4 KB 2732A EPROM containing 256 glyphs in 8×10 dot matrix for 80×25 text on 640×250 raster. |
| **VRAM Mapping Register** | `A8000h` | Memory-mapped latch at `A800:0000`. Bits 0–1 select: `0` = unmapped (open-bus `0xFF`), `1` = mapped at both `B0000h` & `B8000h`, `2` = `B8000h` only (CGA), `3` = `B0000h` only (MDA). |
| **Hyperion CRT Controller** | `A0000h`, Ports `0180h–019Fh`, `03B0h–03DFh`, `IRQ2` (`INT 0Ah`) | 7-inch amber phosphor CRT (640×250 display). Supports Hyperion native text (`80h–83h`), underline, reverse video, blink, intensify, superscript, subscript, double-width; Hyperion native 4-shade amber graphics (`84h/85h`, 320×250) and hi-res (`86h`, 640×250); and standard IBM CGA/MDA modes (`00h–07h`). |
| **ST-506 / ST-412 Hard Disk (C:)** | Ports `0320h–0323h`, `IRQ5` (`INT 0Dh`), BIOS `INT 13h` Drive `80h` | Xebec/WD ST-506 MFM Hard Disk Controller emulation with Option ROM BIOS hook at `C800:0000`. Supports ST-412 (10 MB, 306 cyls, 4 heads, 17 sectors) and ST-225 (20 MB, 615 cyls, 4 heads, 17 sectors). |
| **NEC µPD765 / Intel 8272A FDC** | Ports `00C0h` (MSR), `00C1h` (FIFO), `00C8h` (Control), `0061h` (Status), `IRQ6` (`INT 0Eh`), DMA Ch 2 | Dual 5.25" floppy controller supporting raw `.IMG`/`.DSK` and ImageDisk `.IMD` with sector interleave, track-stepping, and write-protect emulation. |
| **Hyperion Keyboard Interface** | Port `00E0h`, `IRQ1` (`INT 09h`) | Inverted strobe/acknowledge port with hardware self-test (`00h -> FFh -> AAh`) and translation via BIOS table at `FE00:0714`. Optional PC Port `0060h` shim available for standard IBM PC software. |
| **8255 PPI + Oki MSM5832 RTC** | Ports `0140h–0143h` | Battery-backed RTC accessed via 8255 PPI Ports A & C, synchronized with host system clock for `DATE` and `CLOCK.SYS`. |
| **Core Support Peripherals** | Ports `0000h–0043h`, `0080h–0083h`, `0100h–0103h` | 8259A PIC (`IRQ0–7`), 8253 PIT (Timer 0 18.2 Hz system tick, Timer 1 DRAM refresh, Timer 2 PC speaker), 8237A DMAC, and Z80-SIO serial port. |

---

## 3. Hard Disk & Self-Boot Architecture

### Direct Self-Booting from Hard Disk `C:`
- When floppy drives `A:` and `B:` are empty (ejected), the BIOS INT 19h boot sequence falls through to the Hard Disk Option ROM at `C800:0000`.
- The Option ROM loads Master Boot Record (MBR) sector 0 (LBA 0) to `0000:7C00` and executes it.
- The MBR parses the partition table at offset `0x01BE`. Partition 1 is marked active (`0x80`), starts at LBA 17 (`0x2200`), and contains 20,791 sectors (DOS 12-bit FAT partition).
- The MBR loads the DOS VBR (Volume Boot Record) from LBA 17, which initializes FAT12 parameters, loads `IO.SYS` and `MSDOS.SYS`, and boots MS-DOS 2.11 directly from drive `C:`.

### Bundled Hard Disk Image (`web/disks/HYP10MB.IMG`)
Pre-formatted 10,653,696-byte ST-412 image containing:
- **System Files**: `IO.SYS`, `MSDOS.SYS`, `COMMAND.COM` (MS-DOS 2.11)
- **Hyperion Drivers**: `CONFIG.SYS`, `AUTOEXEC.BAT`, `ANSI.SYS`, `CLOCK.SYS`, `EXTVIDEO.SYS`, `RAMDISK.SYS`, `QUICKEYS.COM`, `MSPOOL2.COM`
- **DOS Utilities**: `FDISK.EXE`, `FORMAT.COM`, `CHKDSK.COM`, `DEBUG.COM`, `SYS.COM`, `MODE.EXE`, `MODE.HLP`, `SORT.EXE`, `FIND.EXE`, `LINK.EXE`, `EXE2BIN.EXE`
- **Application Software**:
  - `BASICA.EXE` (Microsoft Advanced BASIC 2.0, 56,832 bytes)
  - `SURF3D.BAS` (Dynalogic 3D Wireframe Surface Graphics Benchmark)
  - `SURF3D.BAT` (Automated launcher batch script)

---

## 4. Dynalogic 3D Wireframe Surface Benchmark (`SURF3D`)

Reconstructed from the canonical 1983 Dynalogic benchmark (featured in contemporary press and Byte Magazine):

### Five Simultaneous Viewports
1. **Top-Left (Saddle Surface)**:
   $$z = \frac{x^2 - y^2}{8}$$
   Center `(188, 68)`, viewport bounds `[60, 17] - [315, 120]`.
2. **Top-Right (Sloping Sinusoidal Wave Sheet)**:
   $$z = 20 \cos\left(\frac{x}{3}\right) \sin\left(\frac{y}{3.8}\right) + 1.2y$$
   Center `(452, 68)`, viewport bounds `[325, 17] - [580, 120]`.
3. **Bottom-Left (Concentric Ripple Sombrero)**:
   $$z = \frac{26 \sin(r / 1.2)}{r / 2.2}, \quad r = \sqrt{x^2 + y^2} + 0.001$$
   Center `(188, 182)`, viewport bounds `[60, 130] - [315, 233]`.
4. **Bottom-Right (Harmonic Ripple Trough)**:
   $$z = 16 \sin\left(\frac{x}{2.8}\right) \cos\left(\frac{y}{3}\right)$$
   Center `(452, 182)`, viewport bounds `[325, 130] - [580, 233]`.
5. **Center Inset (Aerodynamic Perspective Mesh)**:
   $$z = 22 \cos\left(\frac{\sqrt{1.8x^2 + y^2}}{1.8}\right) e^{-|y|/6}$$
   Center `(320, 126)`, overlapping background mask `LINE (195, 72)-(445, 178), 0, BF` with border box `LINE (195, 72)-(445, 178), 1, B`.

### Robust Execution & Prompt Synchronization
- **Buffer Drainage**: Line `585 IF INKEY$<>"" THEN 585` drains pending keystrokes before line 590 waits for user exit (`A$=INKEY$: IF A$="" THEN 590`). This prevents keystrokes typed at the command line from prematurely closing the graphics display.
- **Frontend Sync (`fastForwardToPrompt`)**: Automatically detects `DATE` and `TIME` prompts during boot, submits them, fast-forwards through TSR loading (`QUICKEYS`, `MSPOOL2`), and only types `SURF3D\r` when the `C>` prompt has fully settled.

---

## 5. WebAssembly Frontend & Build System

- **Compiler**: `clang` targeting `wasm32-unknown-unknown` with `-nostdlib -Wl,--no-entry -Wl,--export-dynamic -O3`.
- **Footprint**: Compiled `web/hyperion.wasm` is 74 KB.
- **Build Targets**:
  - `make wasm`: Compiles `src/hyperion.c` to `web/hyperion.wasm`.
  - `make test`: Builds and runs native CLI regression tests (`test_boot` and `test_hd`).
  - `make serve`: Launches local HTTP server on port 8080.
- **CI/CD**: GitHub Actions workflow (`.github/workflows/deploy.yml`) builds wasm, runs unit tests, stamps cache-busting build IDs, and publishes directly to GitHub Pages.

---

## 6. Verification & Automated Tests

All tests run via `make test`:
1. **Floppy Boot Verification (`tests/test_boot.c`)**:
   - Boots `HYPDOS21.IMD`.
   - Passes Date and Time prompts.
   - Runs `DIR` and asserts FAT directory parsing, volume free space, and softkey bar rendering.
2. **Hard Disk & Graphics Regression (`tests/test_hd.c`)**:
   - Mounts `HYP10MB.IMG` on controller.
   - Verifies floppy DOS can read `C:` directory.
   - Ejects floppies and verifies cold reset self-boot directly off `C:`.
   - Launches `SURF3D`, verifies mode switch to CGA Mode 6 (`0x06`), and validates pixel rendering across all viewports.
   - Tests dynamic blank 10MB/20MB image creation and eject routines.

---

## 7. Key File Index

- [`src/hyperion.c`](file:///Users/alans/dev2/Hyperion/src/hyperion.c): Motherboard peripherals, CPU loop, CRT renderer, and WASM exports.
- [`src/hyperion.h`](file:///Users/alans/dev2/Hyperion/src/hyperion.h): Public emulator C / WASM interface definitions.
- [`tests/test_boot.c`](file:///Users/alans/dev2/Hyperion/tests/test_boot.c): Floppy boot automated CLI test.
- [`tests/test_hd.c`](file:///Users/alans/dev2/Hyperion/tests/test_hd.c): Hard disk self-boot and graphics test.
- [`web/app.js`](file:///Users/alans/dev2/Hyperion/web/app.js): WebAssembly frontend, keyboard mapper, audio, and preset controller.
- [`web/index.html`](file:///Users/alans/dev2/Hyperion/web/index.html): Amber CRT view, toolbar macros, and softkeys.
- [`web/disks/HYP10MB.IMG`](file:///Users/alans/dev2/Hyperion/web/disks/HYP10MB.IMG): Bootable 10MB hard disk image with DOS 2.11 and SURF3D.
- [`README.md`](file:///Users/alans/dev2/Hyperion/README.md): Primary user documentation and project overview.
