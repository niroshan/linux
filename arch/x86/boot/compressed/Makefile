# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/x86/boot/compressed/Makefile
#
# create a compressed vmlinux image from the original vmlinux
#
# vmlinuz is:
#	decompression code (*.o)
#	asm globals (piggy.S), including:
#		vmlinux.bin.(gz|bz2|lzma|...)
#
# vmlinux.bin is:
#	vmlinux stripped of debugging and comments
# vmlinux.bin.all is:
#	vmlinux.bin + vmlinux.relocs
# vmlinux.bin.(gz|bz2|lzma|...) is:
#	(see scripts/Makefile.lib size_append)
#	compressed vmlinux.bin.all + u32 size of vmlinux.bin.all

targets := vmlinux vmlinux.bin vmlinux.bin.gz vmlinux.bin.bz2 vmlinux.bin.lzma \
	vmlinux.bin.xz vmlinux.bin.lzo vmlinux.bin.lz4 vmlinux.bin.zst

# CLANG_FLAGS must come before any cc-disable-warning or cc-option calls in
# case of cross compiling, as it has the '--target=' flag, which is needed to
# avoid errors with '-march=i386', and future flags may depend on the target to
# be valid.
KBUILD_CFLAGS := -m$(BITS) -O2 $(CLANG_FLAGS)
KBUILD_CFLAGS += -std=gnu11
KBUILD_CFLAGS += -fno-strict-aliasing -fPIE
KBUILD_CFLAGS += -Wundef
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
cflags-$(CONFIG_X86_32) := -march=i386
cflags-$(CONFIG_X86_64) := -mcmodel=small -mno-red-zone
KBUILD_CFLAGS += $(cflags-y)
KBUILD_CFLAGS += -mno-mmx -mno-sse
KBUILD_CFLAGS += -ffreestanding -fshort-wchar
KBUILD_CFLAGS += -fno-stack-protector
KBUILD_CFLAGS += $(call cc-disable-warning, address-of-packed-member)
KBUILD_CFLAGS += $(call cc-disable-warning, gnu)
KBUILD_CFLAGS += -Wno-pointer-sign
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables
KBUILD_CFLAGS += -D__DISABLE_EXPORTS
# Disable relocation relaxation in case the link is not PIE.
KBUILD_CFLAGS += $(call cc-option,-Wa$(comma)-mrelax-relocations=no)
KBUILD_CFLAGS += -include $(srctree)/include/linux/hidden.h

# sev-decode-insn.c indirectly includes inat-table.c which is generated during
# compilation and stored in $(objtree). Add the directory to the includes so
# that the compiler finds it even with out-of-tree builds (make O=/some/path).
CFLAGS_sev-handle-vc.o += -I$(objtree)/arch/x86/lib/

KBUILD_AFLAGS  := $(KBUILD_CFLAGS) -D__ASSEMBLY__

KBUILD_LDFLAGS := -m elf_$(UTS_MACHINE)
KBUILD_LDFLAGS += $(call ld-option,--no-ld-generated-unwind-info)
# Compressed kernel should be built as PIE since it may be loaded at any
# address by the bootloader.
LDFLAGS_vmlinux := -pie $(call ld-option, --no-dynamic-linker)
ifdef CONFIG_LD_ORPHAN_WARN
LDFLAGS_vmlinux += --orphan-handling=$(CONFIG_LD_ORPHAN_WARN_LEVEL)
endif
LDFLAGS_vmlinux += -z noexecstack
ifeq ($(CONFIG_LD_IS_BFD),y)
LDFLAGS_vmlinux += $(call ld-option,--no-warn-rwx-segments)
endif
ifeq ($(CONFIG_EFI_STUB),y)
# ensure that the static EFI stub library will be pulled in, even if it is
# never referenced explicitly from the startup code
LDFLAGS_vmlinux += -u efi_pe_entry
endif
LDFLAGS_vmlinux += -T

hostprogs	:= mkpiggy
HOST_EXTRACFLAGS += -I$(srctree)/tools/include

sed-voffset := -e 's/^\([0-9a-fA-F]*\) [ABbCDGRSTtVW] \(_text\|__start_rodata\|__bss_start\|_end\)$$/\#define VO_\2 _AC(0x\1,UL)/p'

quiet_cmd_voffset = VOFFSET $@
      cmd_voffset = $(NM) $< | sed -n $(sed-voffset) > $@

targets += ../voffset.h

$(obj)/../voffset.h: vmlinux FORCE
	$(call if_changed,voffset)

$(obj)/misc.o: $(obj)/../voffset.h

vmlinux-objs-y := $(obj)/vmlinux.lds $(obj)/kernel_info.o $(obj)/head_$(BITS).o \
	$(obj)/misc.o $(obj)/string.o $(obj)/cmdline.o $(obj)/error.o \
	$(obj)/piggy.o $(obj)/cpuflags.o

vmlinux-objs-$(CONFIG_EARLY_PRINTK) += $(obj)/early_serial_console.o
vmlinux-objs-$(CONFIG_RANDOMIZE_BASE) += $(obj)/kaslr.o
ifdef CONFIG_X86_64
	vmlinux-objs-y += $(obj)/ident_map_64.o
	vmlinux-objs-y += $(obj)/idt_64.o $(obj)/idt_handlers_64.o
	vmlinux-objs-$(CONFIG_AMD_MEM_ENCRYPT) += $(obj)/mem_encrypt.o
	vmlinux-objs-y += $(obj)/pgtable_64.o
	vmlinux-objs-$(CONFIG_AMD_MEM_ENCRYPT) += $(obj)/sev.o $(obj)/sev-handle-vc.o
endif

vmlinux-objs-$(CONFIG_ACPI) += $(obj)/acpi.o
vmlinux-objs-$(CONFIG_INTEL_TDX_GUEST) += $(obj)/tdx.o $(obj)/tdcall.o $(obj)/tdx-shared.o
vmlinux-objs-$(CONFIG_UNACCEPTED_MEMORY) += $(obj)/mem.o

vmlinux-objs-$(CONFIG_EFI) += $(obj)/efi.o
vmlinux-libs-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a
vmlinux-libs-$(CONFIG_X86_64)	+= $(objtree)/arch/x86/boot/startup/lib.a
vmlinux-objs-$(CONFIG_EFI_SBAT) += $(obj)/sbat.o

ifdef CONFIG_EFI_SBAT
$(obj)/sbat.o: $(CONFIG_EFI_SBAT_FILE)
endif

$(obj)/vmlinux: $(vmlinux-objs-y) $(vmlinux-libs-y) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_vmlinux.bin :=  -R .comment -S
$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

targets += $(patsubst $(obj)/%,%,$(vmlinux-objs-y)) vmlinux.bin.all vmlinux.relocs

CMD_RELOCS = arch/x86/tools/relocs
quiet_cmd_relocs = RELOCS  $@
      cmd_relocs = $(CMD_RELOCS) $< > $@;$(CMD_RELOCS) --abs-relocs $<

$(obj)/vmlinux.relocs: vmlinux.unstripped FORCE
	$(call if_changed,relocs)

vmlinux.bin.all-y := $(obj)/vmlinux.bin
vmlinux.bin.all-$(CONFIG_X86_NEED_RELOCS) += $(obj)/vmlinux.relocs

$(obj)/vmlinux.bin.gz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,gzip)
$(obj)/vmlinux.bin.bz2: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,bzip2_with_size)
$(obj)/vmlinux.bin.lzma: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzma_with_size)
$(obj)/vmlinux.bin.xz: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,xzkern_with_size)
$(obj)/vmlinux.bin.lzo: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lzo_with_size)
$(obj)/vmlinux.bin.lz4: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,lz4_with_size)
$(obj)/vmlinux.bin.zst: $(vmlinux.bin.all-y) FORCE
	$(call if_changed,zstd22_with_size)

suffix-$(CONFIG_KERNEL_GZIP)	:= gz
suffix-$(CONFIG_KERNEL_BZIP2)	:= bz2
suffix-$(CONFIG_KERNEL_LZMA)	:= lzma
suffix-$(CONFIG_KERNEL_XZ)	:= xz
suffix-$(CONFIG_KERNEL_LZO) 	:= lzo
suffix-$(CONFIG_KERNEL_LZ4) 	:= lz4
suffix-$(CONFIG_KERNEL_ZSTD)	:= zst

quiet_cmd_mkpiggy = MKPIGGY $@
      cmd_mkpiggy = $(obj)/mkpiggy $< > $@

targets += piggy.S
$(obj)/piggy.S: $(obj)/vmlinux.bin.$(suffix-y) $(obj)/mkpiggy FORCE
	$(call if_changed,mkpiggy)
