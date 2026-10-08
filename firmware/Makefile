HOSTCC ?= cc
CROSS ?= m68k-elf-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
CFLAGS = -std=c11 -Os -m68000 -ffreestanding -fno-builtin -fno-delete-null-pointer-checks -fno-common -fno-pie -Wall -Wextra -Werror -Iinclude
LDFLAGS = -nostdlib -nostartfiles -Wl,--build-id=none
PANEL = drivers/clock.c platform/timer.S common/debounce.c drivers/ky.c drivers/lcd_hw.c drivers/lcd.c drivers/gate.c drivers/segments.c drivers/analog.c
UPDATER = update/launch.c update/embed.S
COMMON = common/memory.c platform/start.S platform/platform.c drivers/uart.c
.PHONY: all clean test demo
all: build/flash.bin
build:
	mkdir -p $@
build/boot.elf: boot/main.c common/image.c $(UPDATER) $(COMMON) build/update.bin include/board.h ld/boot.ld ld/sections.ld | build
	$(CC) $(CFLAGS) $(LDFLAGS) -T ld/boot.ld -Wl,-Map,build/boot.map $(COMMON) $(UPDATER) common/image.c boot/main.c -lgcc -o $@
build/app.elf: app/main.c $(PANEL) include/panel.h include/timing.h include/debounce.h $(COMMON) include/board.h ld/app.ld ld/sections.ld | build
	$(CC) $(CFLAGS) $(LDFLAGS) -T ld/app.ld -Wl,-Map,build/app.map $(COMMON) $(PANEL) app/main.c -lgcc -o $@
build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@
build/flash.bin: build/boot.bin build/app.bin tools/pack.py
	python3 tools/pack.py
test: test-host test-install test-lcd-service test-serial-tx test-analog-acquire

demo: all
	@echo "Standalone demo image built; emulator integration is maintained separately."
clean:
	$(RM) -r build

LINK = $(UPDATER) common/link.c common/image.c drivers/serial_link.c platform/serial.S
build/link.elf: build/update.bin app/link_main.c $(LINK) $(PANEL) $(COMMON) include/link.h include/lcd_icons.h include/panel.h include/board.h include/debounce.h include/timing.h ld/app.ld ld/sections.ld | build
	$(CC) $(CFLAGS) $(LDFLAGS) -T ld/app.ld -Wl,-Map,build/link.map $(COMMON) $(PANEL) $(LINK) app/link_main.c -lgcc -o $@
build/flash-link.bin: build/boot.bin build/link.bin tools/pack.py
	python3 tools/pack.py --app build/link.bin --output $@
# Same sources with the bring-up sweep compiled in; diagnostic flash only.
build/ladder.elf: build/update.bin app/link_main.c $(LINK) $(PANEL) $(COMMON) include/link.h include/lcd_icons.h include/panel.h include/board.h include/debounce.h include/timing.h ld/app.ld ld/sections.ld | build
	$(CC) $(CFLAGS) -DLADDER $(LDFLAGS) -T ld/app.ld -Wl,-Map,build/ladder.map $(COMMON) $(PANEL) $(LINK) app/link_main.c -lgcc -o $@
build/flash-ladder.bin: build/boot.bin build/ladder.bin tools/pack.py
	python3 tools/pack.py --app build/ladder.bin --output $@
ladder: build/flash-ladder.bin
.PHONY: ladder
protocol: build/flash-link.bin
.PHONY: protocol
test-protocol: protocol
	python3 ../controller/python/test_protocol.py
.PHONY: test-protocol

# Sony EPROM installer: native checks and full isolated serial round trip.
test-install:
	python3 tests/test_install.py
.PHONY: test-install

# Independent RAM image: no calls, data or exception vectors left in flash.
build/update.elf: update/entry.S update/main.c update/engine.c update/hardware.c update/display.c drivers/lcd_hw.c common/image.c update/engine.h update/display.h include/board.h include/panel.h ld/update.ld | build
	$(CC) $(CFLAGS) $(LDFLAGS) -T ld/update.ld -Wl,-Map,build/update.map update/entry.S update/main.c update/engine.c update/hardware.c update/display.c drivers/lcd_hw.c common/image.c -o $@

test-self-update: protocol
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/update_engine.c update/engine.c common/image.c -o build/test-update
	./build/test-update
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/update_display.c update/display.c -o build/test-update-display
	./build/test-update-display
	python3 tests/test_self_update.py
.PHONY: test-self-update

test-analog: | build
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/analog_stream.c common/image.c -o build/test-analog
	./build/test-analog
.PHONY: test-analog

test-lcd-service: | build
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/lcd_service.c -o build/test-lcd-service
	./build/test-lcd-service
.PHONY: test-lcd-service

test-serial-tx: | build
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/serial_tx.c -o build/test-serial-tx
	./build/test-serial-tx
.PHONY: test-serial-tx

test-analog-acquire: | build
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/analog_acquire.c -o build/test-analog-acquire
	./build/test-analog-acquire
.PHONY: test-analog-acquire

# Host-only checks do not need a cross toolchain, panel or Sony ROMs.
test-host: | build
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/debounce.c common/debounce.c -o build/test-debounce
	./build/test-debounce
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/update_engine.c update/engine.c common/image.c -o build/test-update
	./build/test-update
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude tests/update_display.c update/display.c -o build/test-update-display
	./build/test-update-display
	$(MAKE) test-analog
.PHONY: test-host
