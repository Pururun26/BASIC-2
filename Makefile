# ============================================================
#  BASIC-2 fantasy console — Makefile
#  Toolchain : MSYS2 UCRT64  (gcc, make / mingw32-make)
# ============================================================

CC       := gcc
BUILD    := build
OBJ      := $(BUILD)/obj
BIN      := $(BUILD)/b2.exe

SRC_DIR    := src
BS_DIR     := bs
RAYLIB_DIR := raylib/raylib-6.0_win64_mingw-w64
RAYLIB_INC := $(RAYLIB_DIR)/include
RAYLIB_LIB := $(RAYLIB_DIR)/lib

# --- Источники -------------------------------------------------
APP_SRCS := $(wildcard $(SRC_DIR)/*.c)
BS_SRCS  := $(wildcard $(BS_DIR)/*.c)

APP_OBJS := $(patsubst %.c,$(OBJ)/%.o,$(APP_SRCS))
BS_OBJS  := $(patsubst %.c,$(OBJ)/%.o,$(BS_SRCS))
OBJS     := $(APP_OBJS) $(BS_OBJS)
DEPS     := $(OBJS:.o=.d)

# --- Флаги -----------------------------------------------------
CPPFLAGS := -I$(SRC_DIR) -I$(BS_DIR) -I$(RAYLIB_INC) -MMD -MP
CFLAGS   := -std=c11 -Wall -Wextra -O2 -g
LDFLAGS  := -L$(RAYLIB_LIB)
LDLIBS   := -lraylib -lopengl32 -lgdi32 -lwinmm -lm

# --- Цели ------------------------------------------------------
.PHONY: all clean rebuild

all: $(BIN)

rebuild: clean all

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(DEPS)