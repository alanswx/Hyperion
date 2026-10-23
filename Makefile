# Dynalogic Hyperion (1983) Emulator Makefile

ifneq ($(wildcard /opt/homebrew/opt/llvm/bin/clang),)
WASM_CC ?= /opt/homebrew/opt/llvm/bin/clang
else
WASM_CC ?= clang
endif

HOST_CC ?= cc
CFLAGS  ?= -O3 -Wall -Wextra

.PHONY: all wasm web test disks serve clean

all: wasm

wasm: web/hyperion.wasm

web: wasm disks

web/hyperion.wasm: src/hyperion.c src/hyperion.h src/decode_tables.h src/roms.h
	@mkdir -p web
	$(WASM_CC) --target=wasm32 -O3 -c src/hyperion.c -o web/hyperion.wasm
	@ls -lh web/hyperion.wasm

disks:
	@mkdir -p web/disks
	@if [ -d archive/software ]; then \
		cp -f archive/software/*.IMD web/disks/ 2>/dev/null || true; \
		if [ -f "archive/software/Multimate/Hyperion Multimate Master Diskette.imd" ]; then \
			cp -f "archive/software/Multimate/Hyperion Multimate Master Diskette.imd" web/disks/MULTIPLN.IMD; \
		fi; \
	fi

test: wasm
	@mkdir -p build
	$(HOST_CC) $(CFLAGS) src/hyperion.c tests/test_boot.c -o build/test_boot
	./build/test_boot web/disks/HYPMSTRU.IMD

serve: wasm disks
	@echo "Serving Hyperion WebAssembly Emulator at http://localhost:8080/web/"
	python3 -m http.server 8080

clean:
	rm -rf build
