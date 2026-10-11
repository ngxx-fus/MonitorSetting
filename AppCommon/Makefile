# Compiler and flags
CC          ?= gcc
CFLAGS      ?= -Wall -Wextra -g

# Auto-detect all subdirectories and *.c files in ./
BUILD_DIR   ?= build
INC_DIRS    := $(shell find . -type d -not -path "./$(BUILD_DIR)*")
INC_FLAGS   := $(addprefix -I,$(INC_DIRS))

SRCS        := $(shell find . -name "*.c" -not -path "./$(BUILD_DIR)*")
OBJS        := $(patsubst ./%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all build clean

all: build

build: $(OBJS)

$(BUILD_DIR)/%.o: ./%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

