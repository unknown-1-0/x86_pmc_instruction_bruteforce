CC=clang
CPU_MODE=64
TARGET_UARCH=SKYLAKE
TARGET_CFLAGS=-Wl,-entry:efi_main -Wl,-subsystem:efi_application -fuse-ld=lld-link -target x86_64-unknown-windows -nostdlib -ffreestanding -fshort-wchar -mno-red-zone
CFLAGS=$(TARGET_CFLAGS) -Wall -Wextra -Wno-error=strict-prototypes -Wno-error=unused-function -pedantic -Werror -masm=intel -I/usr/include/efi -I./src -I./xed/obj/wkit/include/xed -DSAVE_REGS_ON_EXCEPTION -O3 -DCOUNT_NOPS -DMODE=$(CPU_MODE) -DCOUNT_XED_VS_CPU_MISMATCHES -DTARGET=$(TARGET_UARCH) -flto=full

SRCS=src/main.c src/print.c src/load_segments.S src/exception_handlers.S src/instruction_execution_loop.c src/system_tables_setup.c src/enter_user.S src/save_file.c src/halt.c src/string.c src/stdlib.c src/disasm.c xed/obj/wkit/lib/libxed.a

OUT_IMG=app.img

all: $(OUT_IMG)

xed/obj/wkit/lib/libxed.a:
	./build_xed.sh

BOOTX64.EFI: xed/obj/wkit/lib/libxed.a
	$(CC) $(CFLAGS) -o BOOTX64.EFI $(SRCS)

$(OUT_IMG): clean BOOTX64.EFI
	dd if=/dev/zero of=$(OUT_IMG) bs=1M count=32
	mkfs.fat $(OUT_IMG)
	mmd -i $(OUT_IMG) ::/EFI
	mmd -i $(OUT_IMG) ::/EFI/BOOT
	mcopy -i $(OUT_IMG) BOOTX64.EFI ::/EFI/BOOT

clean:
	rm BOOTX64.EFI $(OUT_IMG) || true
