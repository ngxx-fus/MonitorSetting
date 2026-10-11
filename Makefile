# Root Makefile for MonitorSelection
CC          ?= gcc
SDL_CFLAGS  := $(shell sdl2-config --cflags)
SDL_LIBS    := $(shell sdl2-config --libs) -lSDL2_gfx -lSDL2_ttf -lpthread
CFLAGS      ?= -Wall -Wextra -g $(SDL_CFLAGS)

BUILD_DIR   ?= build
TARGET      ?= AppExec

# Auto-detect all subdirectories and *.c files in ./
INC_DIRS    := $(shell find . -type d -not -path "./$(BUILD_DIR)*" -not -path "./.git*" -not -path "./.CodeTemplate*" -not -path "./docs*")
INC_FLAGS   := $(addprefix -I,$(INC_DIRS))

SRCS        := $(shell find . -name "*.c" -not -path "./$(BUILD_DIR)*" -not -path "./.CodeTemplate*")
OBJS        := $(patsubst ./%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all build clean doc

all: build

build: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(SDL_LIBS)

$(BUILD_DIR)/%.o: ./%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@

doc:
	doxygen Doxyfile

clean:
	rm -rf $(BUILD_DIR) $(TARGET) docs
	@$(MAKE) -C AppCommon clean
	@$(MAKE) -C AppSDL2 clean
