# ============================================================================
# inmp441_rpi - I2S MEMS microphone (INMP441) reader for Raspberry Pi
# Uses the bcm2835 userspace library for raw PCM/I2S peripheral access.
#
# Build layout (obj/ mirrors src/):
#   src/audio/foo.cpp  ->  obj/audio/foo.o
#   src/core/foo.cpp   ->  obj/core/foo.o
# ============================================================================

SHELL      := /bin/bash

PROJECT    := inmp441_rpi
BINARY     := $(PROJECT)

SRC_DIR    := src
INC_DIR    := include
OBJ_DIR    := obj
BIN_DIR    := bin

CXX        ?= g++
CXXSTD     := c++17

# Where the bcm2835 library header lives. The default (/usr/local/include) is
# what scripts/install_dependencies.sh installs into; override on the command
# line for cross-build checks (e.g. BCM2835_INCLUDE=/path/to/stub).
BCM2835_INCLUDE ?= /usr/local/include

WARNINGS   := -Wall -Wextra -Wpedantic -Wshadow
OPT_FLAGS  := -O2
CXXFLAGS   := $(OPT_FLAGS) -std=$(CXXSTD) $(WARNINGS) -I$(INC_DIR) \
              -I$(INC_DIR)/oled -I$(INC_DIR)/sound -I$(INC_DIR)/tools \
              -I$(BCM2835_INCLUDE) -MMD -MP
LDLIBS     := -lbcm2835 -lmpg123 -lao
BCM2835_LIB ?= $(LDLIBS)
LDFLAGS    := $(BCM2835_LIB) -lm -pthread

# Discover sources and mirror the tree into obj/.
SRCS := $(shell find $(SRC_DIR) -type f -name '*.cpp' | sort)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET     := $(BIN_DIR)/$(BINARY)

TEST_SRC   := tests/test_conversion.cpp
TEST_BIN   := $(OBJ_DIR)/test_conversion

.PHONY: all clean run test help

all: $(TARGET)

# ---- Binary ----------------------------------------------------------------

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# ---- Convenience ------------------------------------------------------------

# Runs the binary with sudo (required for /dev/mem access).
# Extra arguments can be passed with ARGS="--wav out.wav -d 10".
run: all
	sudo $(TARGET) $(ARGS)

test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

help:
	@echo "Targets:"
	@echo "  all   - build the binary (default)"
	@echo "  run   - run with sudo (ARGS='--wav x.wav -d 10' for options)"
	@echo "  test  - build and run the conversion unit tests (no hardware)"
	@echo "  clean - remove obj/ and bin/"
	@echo ""
	@echo "First-time setup on the Pi: scripts/install_dependencies.sh"

# Pull in auto-generated dependency files.
-include $(DEPS)
