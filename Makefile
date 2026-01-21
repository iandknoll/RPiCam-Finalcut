# ===========================================================================================
# Makefile for RPiCam-Finalcut repo
# ===========================================================================================

# -------------------------------------------------------------------------------------------
# Setup
# -------------------------------------------------------------------------------------------

# Search directory for source file(s)
SOURCE := $(wildcard *.cpp)

# Target (End Program Name)
TARGET := Reaching_Cam

# Submodule Directories
RPICAM_DIR = rpicam-apps
RPICAM_BUILD_DIR = $(RPICAM_DIR)/build
RPICAM_STAMP = $(RPICAM_BUILD_DIR)/built

FINALCUT_DIR = finalcut
FINALCUT_BUILD_DIR = $(FINALCUT_DIR)/build
FINALCUT_STAMP = $(FINALCUT_BUILD_DIR)/built

# Compiler
CXX := g++

# pkg-cnfoig flags
LIBCAMERA_CFLAGS := $(shell pkg-config --cflags libcamera)
LIBCAMERA_LIBS   := $(shell pkg-config --libs libcamera)

# Compiler flags
CXXFLAGS := -std=c++17 \
            -I$(RPICAM_DIR)\
            -I$(RPICAM_BUILD_DIR)/include \
            $(LIBCAMERA_CFLAGS) \
            -I$(FINALCUT_DIR)

# Additional Compiler flags (if DEBUG=1 set)
ifeq ($(DEBUG),1)
	CXXFLAGS += -g -O0 -fno-omit-frame-pointer \
                    -Wall -Wextra -Wpedantic
else
	CXXFLAGS += -O2
endif

# Libraries and linkers
LDFLAGS := -L$(RPICAM_BUILD_DIR) \
           -lrpicam_app \
           $(LIBCAMERA_LIBS) \
           -L$(FINALCUT_BUILD_DIR)/lib -lfinal \
           -lboost_program_options \
           -pthread -ldl

# -------------------------------------------------------------------------------------------
# Default target (what will run w/o modifiers)
# -------------------------------------------------------------------------------------------

all: check-deps $(TARGET)

# -------------------------------------------------------------------------------------------
# Build rpicam-apps submodule
# -------------------------------------------------------------------------------------------

# Runs automatically if rpicam-apps not built

$(RPICAM_STAMP):
	@command -v meson >/dev/null || \
		(echo "Error: meson not found. Install with: sudo apt install meson"; exit 1)
	@command -v ninja >/dev/null || \
		(echo "Error: ninja not found. Install with: sudo apt install ninja-build"; exit 1)

	@echo "Building rpicam-apps submodule..."
	cd $(RPICAM_DIR) && \
	meson setup build \
		-Denable_egl=disabled \
		-Denable_drm=disabled \
		-Denable_qt=disabled && \
	ninja -C build && \
	touch $@

# -------------------------------------------------------------------------------------------
# Build FinalCut submodule
# -------------------------------------------------------------------------------------------

# Runs automatically if Finalcut not built

$(FINALCUT_STAMP):
	@echo "Building finalcut submodule..."
	cd $(FINALCUT_DIR) && \
	autoreconf --install --force && \
	mkdir -p build && \
	cd build && \
	../configure \
		 --prefix=$$(pwd) \
		CPPFLAGS="-I$$(cd .. && pwd)" && \
	make && make install && \
	touch $@

# -------------------------------------------------------------------------------------------
# Build main program
# -------------------------------------------------------------------------------------------

# To use: "make"
$(TARGET): $(SOURCE) $(RPICAM_STAMP) $(FINALCUT_STAMP)
	@echo "Building $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET) $(LDFLAGS)

# -------------------------------------------------------------------------------------------
# Extra Build Modes
# -------------------------------------------------------------------------------------------

# To use: "make debug"
debug:
	$(MAKE) DEBUG=1

# To use: "make release"
release:
	$(MAKE) DEBUG=0

# -------------------------------------------------------------------------------------------
# Verify dependencies are present
# -------------------------------------------------------------------------------------------

check-deps:
	@echo "Checking build dependencies..."
	@command -v $(CXX) >/dev/null || \
                (echo "Missing compiler: g++ (install build-essential)"; exit 1)
	@command -v meson >/dev/null || \
                (echo "Missing tool: meson"; exit 1)
	@command -v ninja >/dev/null || \
                (echo "Missing tool: ninja (package: ninja-build)"; exit 1)
	@command -v pkg-config >/dev/null || \
                (echo "Missing tool: pkg-config"; exit 1)

	@pkg-config --exists libcamera || \
                (echo "Missing dependency: libcamera-dev"; exit 1)
	@dpkg -s libboost-all-dev >/dev/null 2>&1 || \
                (echo "Missing dependency: libboost-all-dev"; exit 1)
	@pkg-config --exists libdrm || \
                (echo "Missing dependency: libdrm-dev"; exit 1)
	@pkg-config --exists libexif || \
                (echo "Missing dependency: libexif-dev"; exit 1)
	@pkg-config --exists libpng || \
                (echo "Missing dependency: libpng-dev"; exit 1)
	@pkg-config --exists libjpeg || \
                (echo "Missing dependency: libjpeg-dev"; exit 1)
	@pkg-config --exists libtiff-4 || \
                (echo "Missing dependency: libtiff5-dev"; exit 1)

	@echo "All required dependencies are installed."

# -------------------------------------------------------------------------------------------
# help function
# -------------------------------------------------------------------------------------------

# To use: "make help"
help:
	@echo "Available targets:"
	@echo "  make               - Build (release)"
	@echo "  make debug         - Debug build"
	@echo "  make run           - Run the program"
	@echo "  make clean         - Remove local build artifacts"
	@echo "  make distclean     - Remove rpicam-apps & finalcut build"
	@echo "  make check-deps    - Verify dependencies"

# -------------------------------------------------------------------------------------------
# Run program (w/ proper library path)
# -------------------------------------------------------------------------------------------

# To use: "make run"
run: $(TARGET)
	@echo "Running $(TARGET)..."
	LD_LIBRARY_PATH=$(RPICAM_BUILD_DIR):$(FINALCUT_BUILD_DIR)/lib ./$(TARGET)

# -------------------------------------------------------------------------------------------
# Cleanup
# -------------------------------------------------------------------------------------------

# To use: "make clean"
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGET)

# To use: "make distclean"
distclean: clean
	@echo "Removing rpicam-apps build..."
	rm -rf $(RPICAM_BUILD_DIR)
	@echo "Removing finalcut build..."
	rm -rf $(FINALCUT_BUILD_DIR)
	rm -f $(RPICAM_STAMP) $(FINALCUT_STAMP)

# -------------------------------------------------------------------------------------------
# Force rebuild everything (runs both clean, then build)
# -------------------------------------------------------------------------------------------

# To use: "make rebuild"
rebuild: clean all

# -------------------------------------------------------------------------------------------
# Phony targets
# -------------------------------------------------------------------------------------------

# Defines all run conditions
# (i.e. what we can include after "make")
.PHONY: all debug release run clean distclean rebuild check-deps help
