ifndef MPY_DIR
$(error MPY_DIR must point to the micropython directory)
endif

# Name of module
MOD = ugrey

# Source files (.c or .py)
SRC = ugrey.py ugrey.c framebuffer.c

# Architecture to build for (x86, x64, armv6m, armv7m, xtensa, xtensawin)
ARCH = armv6m

# Include to get the rules for compiling and linking the module
include $(MPY_DIR)/py/dynruntime.mk

#CFLAGS += -fno-builtin
#CFLAGS += -fno-builtin-function
CFLAGS += -fno-jump-tables
CFLAGS += -ffreestanding

CFLAGS += -Wno-error

