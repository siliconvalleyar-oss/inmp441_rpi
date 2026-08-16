# Makefile - Grabador/Reproductor INMP441 para Raspberry Pi
# Compatible con Raspberry Pi OS 32-bit (armhf, ej. Pi Zero 2W) y
# 64-bit (arm64/aarch64, ej. Pi 4). No hay flags de arquitectura fijos:
# cada Pi compila nativo con su propio toolchain de g++.
#
# Estructura generada:
#   bin/inmp441_recorder   -> binario final
#   obj/<mismo path que en src/>.o -> un .o por cada .cpp, replicando
#                                       la jerarquía de carpetas de src/
#
# Uso:
#   make            compila (Release por defecto)
#   make DEBUG=1    compila con símbolos de debug y sin optimizar
#   make run        compila y ejecuta el menú interactivo
#   make install    instala el binario en /usr/local/bin
#   make clean      borra bin/ y obj/
#   make distclean  además borra output/ (¡borra tus grabaciones!)

# ---------------------------------------------------------------------------
# Herramientas y flags
# ---------------------------------------------------------------------------
CXX      := g++
PKGCFG   := pkg-config

TARGET_NAME := inmp441_recorder

SRC_DIR    := src
INC_DIR    := include
OBJ_DIR    := obj
BIN_DIR    := bin
OUTPUT_DIR := output

CXXSTD   := -std=c++17
WARN     := -Wall -Wextra
DEPFLAGS := -MMD -MP

ifeq ($(DEBUG),1)
    OPT := -O0 -g3 -DDEBUG
else
    OPT := -O2
endif

# Flags de las librerías del sistema (ALSA + libgpiod) vía pkg-config.
# Esto resuelve automáticamente include paths y libs correctas sin
# importar si el sistema es armhf o arm64.
PKG_LIBS      := alsa libgpiod
PKG_CFLAGS    := $(shell $(PKGCFG) --cflags $(PKG_LIBS) 2>/dev/null)
PKG_LDLIBS    := $(shell $(PKGCFG) --libs $(PKG_LIBS) 2>/dev/null)

ifeq ($(strip $(PKG_CFLAGS))$(strip $(PKG_LDLIBS)),)
    $(warning No se encontraron alsa/libgpiod via pkg-config. \
Instalá: sudo apt install libasound2-dev libgpiod-dev pkg-config)
endif

CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(DEPFLAGS) -I$(INC_DIR) $(PKG_CFLAGS)
LDLIBS   := $(PKG_LDLIBS)

# ---------------------------------------------------------------------------
# Descubrimiento de fuentes (replica la estructura de src/ en obj/)
# ---------------------------------------------------------------------------
SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

TARGET := $(BIN_DIR)/$(TARGET_NAME)

# ---------------------------------------------------------------------------
# Reglas
# ---------------------------------------------------------------------------
.PHONY: all run install uninstall clean distclean dirs print-arch help

all: print-arch dirs $(TARGET)

# Info de diagnóstico: útil para confirmar en qué Pi (32/64 bit) se compiló.
print-arch:
	@echo ">> Compilando para: $$(uname -m) ($$(getconf LONG_BIT)-bit) - $$(g++ --version | head -n1)"

dirs:
	@mkdir -p $(BIN_DIR) $(OUTPUT_DIR)

# Link final: junta todos los .o en el binario dentro de bin/
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDLIBS)
	@echo ">> Binario generado: $@"

# Compilación de cada .cpp a su .o correspondiente dentro de obj/,
# creando las subcarpetas necesarias para replicar src/ (patrón genérico,
# soporta subdirectorios futuros como src/platform/pi4/foo.cpp).
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Incluye los .d generados por -MMD para recompilar sólo lo necesario
# cuando cambian headers.
-include $(DEPS)

run: all
	./$(TARGET)

install: all
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET_NAME)
	@echo ">> Instalado en /usr/local/bin/$(TARGET_NAME)"

uninstall:
	rm -f /usr/local/bin/$(TARGET_NAME)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

distclean: clean
	rm -rf $(OUTPUT_DIR)

help:
	@echo "Targets disponibles:"
	@echo "  make            - compila (bin/$(TARGET_NAME), objetos en obj/)"
	@echo "  make DEBUG=1    - compila con símbolos de debug"
	@echo "  make run        - compila y ejecuta"
	@echo "  make install    - instala en /usr/local/bin"
	@echo "  make uninstall  - desinstala"
	@echo "  make clean      - borra bin/ y obj/"
	@echo "  make distclean  - clean + borra output/ (grabaciones)"
