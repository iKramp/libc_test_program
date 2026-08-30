# test program makefile
# Toolchain
CC      ?= gcc

# Directories
SRC_DIR   := src
BUILD_DIR := build

TARGET := test

# Compiler flags
CFLAGS += \
    -std=c23 \
    -Wall \
    -Wextra \
    -ffreestanding \
    -nostdlib \
	-nostartfiles \
    -fno-stack-protector \
    -static \
	-fno-tree-vectorize \
	-fno-tree-slp-vectorize

# Linker flags
LDFLAGS += -nostdlib -static -nostartfiles

# Sources
SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')

# Objects
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $$MY_LIBC/lib/crt0.o $^ -L$$MY_LIBC/lib -lc -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) \
	    -MMD -MP \
	    -c $< \
	    -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
