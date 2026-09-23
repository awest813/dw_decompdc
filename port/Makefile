# Non-matching port build of Digimon World. See port/README.md.
#
#   make -C port              SH-4 compile + link check (sh4-linux-gnu toolchain)
#   make -C port run          run that build under qemu-sh4 (stubbed SDK, no disc)
#   make -C port test         unit tests (host; also SH-4 through qemu if present)
#   make -C port PLATFORM=dc  Dreamcast ELF (needs a KallistiOS environment)
#
# The matching build (top-level Makefile) is not affected: game sources only
# change behind DW_PORT.

ROOT := $(abspath ..)
PORT := $(ROOT)/port
PLATFORM ?= check
BUILD := $(ROOT)/build/port-$(PLATFORM)

PYTHON ?= python3
QEMU ?= qemu-sh4-static
RUN_SECONDS ?= 10

ifeq ($(V),1)
Q :=
MSG := @true
else
Q := @
MSG := @echo
endif

ifeq ($(PLATFORM),dc)
# KallistiOS: source environ.sh first. Not yet exercised by any automated build.
ifndef KOS_BASE
$(error PLATFORM=dc needs a KallistiOS environment (source environ.sh))
endif
CC := $(KOS_CC)
AS := $(KOS_AS)
NM := $(KOS_NM)
OBJCOPY := $(KOS_OBJCOPY)
ARCH_CFLAGS := $(KOS_CFLAGS)
ARCH_LDFLAGS := $(KOS_LDFLAGS) $(KOS_START) $(KOS_LIB_PATHS)
ARCH_LIBS := -Wl,--start-group $(KOS_LIBS) -Wl,--end-group
GLUE_LIBS := $(KOS_BASE)/lib/dreamcast/libkallisti.a
else
CROSS ?= sh4-linux-gnu-
CC := $(CROSS)gcc
AS := $(CROSS)as
NM := $(CROSS)nm
OBJCOPY := $(CROSS)objcopy
ARCH_CFLAGS := -ml -m4
ARCH_LDFLAGS := -static
ARCH_LIBS :=
GLUE_LIBS :=
endif

GLUE_LIBS += $(shell $(CC) $(ARCH_CFLAGS) -print-file-name=libc.a) \
	     $(shell $(CC) $(ARCH_CFLAGS) -print-libgcc-file-name)
PREFIX := $(shell echo __USER_LABEL_PREFIX__ | $(CC) $(ARCH_CFLAGS) -E -P -x c - | tr -d ' \n')

# Keep the original semantics as far as GCC allows (see
# docs/dreamcast_port_audit.md, section 3.4).
SEMANTIC_FLAGS := -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks \
		  -fno-aggressive-loop-optimizations -fno-toplevel-reorder \
		  -fno-common -fno-zero-initialized-in-bss

# Game code and SDK replacements: the PsyQ header world, port overrides first.
PSYQ_CFLAGS := $(ARCH_CFLAGS) -O2 -std=gnu99 -nostdinc -DDW_PORT -DLANGUAGE_C \
	       $(SEMANTIC_FLAGS) \
	       -I$(PORT)/include -I$(ROOT)/include \
	       -I$(ROOT)/external/psyq_headers/mw_lib41/include
GAME_CFLAGS := $(PSYQ_CFLAGS) -w
# PsyQ headers declare functions such as csin() that clash with GCC builtins.
SDK_CFLAGS := $(PSYQ_CFLAGS) -Wall -Wno-unknown-pragmas -Wno-builtin-declaration-mismatch

# Port runtime: the toolchain's own C library headers.
PORT_CFLAGS := $(ARCH_CFLAGS) -O2 -std=gnu99 -DDW_PORT -Wall -Wextra

# Header dependencies
DEPFLAGS := -MMD -MP
GAME_CFLAGS += $(DEPFLAGS)
SDK_CFLAGS += $(DEPFLAGS)
PORT_CFLAGS += $(DEPFLAGS)

GAME_SRC := $(filter-out $(ROOT)/src/main/_psstart.c $(ROOT)/src/main/swap.c, \
	    $(sort $(wildcard $(ROOT)/src/*/*.c)))
SDK_SRC := $(sort $(wildcard $(PORT)/psyq/*.c))
SDK_SRC := $(filter-out $(PORT)/psyq/libc.c,$(SDK_SRC))
RUNTIME_SRC := $(PORT)/psyq/libc.c $(sort $(wildcard $(PORT)/platform/*.c))

GAME_OBJ := $(GAME_SRC:$(ROOT)/%.c=$(BUILD)/%.o)
SDK_OBJ := $(SDK_SRC:$(ROOT)/%.c=$(BUILD)/%.o)
RUNTIME_OBJ := $(RUNTIME_SRC:$(ROOT)/%.c=$(BUILD)/%.o)
OBJ := $(GAME_OBJ) $(SDK_OBJ) $(RUNTIME_OBJ)

GLUE_ASM := $(BUILD)/glue/bss.s
GLUE_STUBS := $(BUILD)/glue/stubs.c
GLUE_OBJ := $(BUILD)/glue/bss.o $(BUILD)/glue/stubs.o
ELF := $(BUILD)/dw.elf

all: $(ELF)

$(ELF): $(OBJ) $(GLUE_OBJ)
	$(MSG) "  LD      $(@:$(ROOT)/%=%)"
	$(Q)$(CC) $(ARCH_CFLAGS) -o $@ $(ARCH_LDFLAGS) $(OBJ) $(GLUE_OBJ) $(ARCH_LIBS)

# The game's main() becomes dw_game_main(); the port runtime owns main().
$(BUILD)/src/main/main.o: GAME_CFLAGS += -Dmain=dw_game_main

$(BUILD)/src/main/%.o: $(ROOT)/src/main/%.c
	@mkdir -p $(dir $@)
	$(MSG) "  CC      $(<:$(ROOT)/%=%)"
	$(Q)$(CC) $(GAME_CFLAGS) -c -o $@ $<

# Overlay objects: group .data/.bss per overlay so that the runtime can
# restore them when the game "loads" the overlay (port/platform/port_core.c).
$(BUILD)/src/%.o: $(ROOT)/src/%.c
	@mkdir -p $(dir $@)
	$(MSG) "  CC      $(<:$(ROOT)/%=%)"
	$(Q)$(CC) $(GAME_CFLAGS) -c -o $@ $<
	$(Q)$(OBJCOPY) --rename-section .data=dw_ovl_$(firstword $(subst /, ,$*))_data \
		   --rename-section .bss=dw_ovl_$(firstword $(subst /, ,$*))_bss $@

$(BUILD)/port/psyq/libc.o: $(PORT)/psyq/libc.c
	@mkdir -p $(dir $@)
	$(MSG) "  CC      $(<:$(ROOT)/%=%)"
	$(Q)$(CC) $(PORT_CFLAGS) -c -o $@ $<

$(BUILD)/port/psyq/%.o: $(PORT)/psyq/%.c
	@mkdir -p $(dir $@)
	$(MSG) "  CC      $(<:$(ROOT)/%=%)"
	$(Q)$(CC) $(SDK_CFLAGS) -c -o $@ $<

$(BUILD)/port/platform/%.o: $(PORT)/platform/%.c
	@mkdir -p $(dir $@)
	$(MSG) "  CC      $(<:$(ROOT)/%=%)"
	$(Q)$(CC) $(PORT_CFLAGS) -c -o $@ $<

$(GLUE_ASM) $(GLUE_STUBS) &: $(OBJ) $(PORT)/tools/gen_glue.py \
		$(ROOT)/config/symbols.txt $(ROOT)/config/bss.yaml $(ROOT)/config/sbss.yaml
	@mkdir -p $(dir $@)
	$(MSG) "  GLUE    $(BUILD:$(ROOT)/%=%)/glue"
	$(Q)$(PYTHON) $(PORT)/tools/gen_glue.py --nm $(NM) --prefix '$(PREFIX)' \
		--symbols $(ROOT)/config/symbols.txt \
		--bss $(ROOT)/config/bss.yaml --sbss $(ROOT)/config/sbss.yaml \
		$(addprefix --lib ,$(GLUE_LIBS)) \
		--out-asm $(GLUE_ASM) --out-stubs $(GLUE_STUBS) \
		--report $(BUILD)/glue/report.txt $(OBJ)

$(BUILD)/glue/bss.o: $(GLUE_ASM)
	$(Q)$(CC) $(ARCH_CFLAGS) -c -o $@ $<

$(BUILD)/glue/stubs.o: $(GLUE_STUBS)
	$(Q)$(CC) $(PORT_CFLAGS) -Wno-old-style-definition -Wno-strict-prototypes -c -o $@ $<

run: $(ELF)
	$(Q)timeout $(RUN_SECONDS) $(QEMU) $(ELF); status=$$?; \
	if [ $$status -eq 124 ]; then echo "(stopped after $(RUN_SECONDS)s)"; else exit $$status; fi

# ---- unit tests --------------------------------------------------------

TEST_BUILD := $(ROOT)/build/port-test
HOSTCC ?= cc
SH4_TEST_CC ?= sh4-linux-gnu-gcc
GTE_TEST_SRC := $(PORT)/tests/test_gte.c $(PORT)/psyq/gte.c
# libgte needs the PsyQ types (32-bit long), so it is only tested on SH-4.
LIBGTE_TEST_SRC := $(PORT)/tests/test_libgte.c $(PORT)/psyq/libgte.c \
		   $(PORT)/psyq/gte.c $(PORT)/psyq/gte_tables.c
LIBGTE_TEST_CFLAGS := -ml -m4 -O2 -std=gnu99 -nostdinc -DLANGUAGE_C -Wall \
		      -Wno-builtin-declaration-mismatch \
		      -I$(ROOT)/external/psyq_headers/mw_lib41/include

test:
	@mkdir -p $(TEST_BUILD)
	$(Q)$(HOSTCC) -O2 -Wall -Wextra -o $(TEST_BUILD)/test_gte $(GTE_TEST_SRC)
	$(Q)$(TEST_BUILD)/test_gte
	$(Q)if command -v $(SH4_TEST_CC) >/dev/null 2>&1 && command -v $(QEMU) >/dev/null 2>&1; then \
		echo "SH-4 (through $(QEMU)):"; \
		$(SH4_TEST_CC) -O2 -Wall -Wextra -static -o $(TEST_BUILD)/test_gte.sh4 $(GTE_TEST_SRC) && \
		$(QEMU) $(TEST_BUILD)/test_gte.sh4 && \
		$(SH4_TEST_CC) $(LIBGTE_TEST_CFLAGS) -static -o $(TEST_BUILD)/test_libgte.sh4 $(LIBGTE_TEST_SRC) && \
		$(QEMU) $(TEST_BUILD)/test_libgte.sh4; \
	else \
		echo "(skipping SH-4 tests: $(SH4_TEST_CC) or $(QEMU) not found)"; \
	fi

clean:
	rm -rf $(BUILD) $(TEST_BUILD)

-include $(OBJ:.o=.d)

.PHONY: all run test clean
