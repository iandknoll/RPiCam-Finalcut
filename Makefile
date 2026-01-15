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

# Compiler
CXX := g++

# Compiler flags
CXXFLAGS := -std=c++17 \
            -I$(RPICAM_BUILD_DIR)/include

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
           -pthread -ldl

# -------------------------------------------------------------------------------------------
# Default target (what will run w/o modifiers)
# -------------------------------------------------------------------------------------------

all: check-deps $(TARGET)

# -------------------------------------------------------------------------------------------
# Build rpicam-apps submodule
# -------------------------------------------------------------------------------------------

# To use: "make rpicam-apps"

rpicam-apps:
	@command -v meson >/dev/null || \
		(echo "Error: meson not found. Install with: sudo apt install meson"; exit 1)
	@command -v ninja >/dev/null || \
		(echo "Error: ninja not found. Install with: sudo apt install ninja-build"; exit 1)

	@echo "Building rpicam-apps submodule..."
	@if [ ! -d "$(RPICAM_BUILD_DIR)" ]; then \
		cd $(RPICAM_DIR) && meson setup build \
			-Deql=disable -Ddrm=disable -Dqt=disable; \
	else \
		cd $(RPICAM_DIR) && meson setup build --reconfigure; \
	fi
	cd $(RPICAM_DIR) && ninja -C build

# -------------------------------------------------------------------------------------------
# Build main program
# -------------------------------------------------------------------------------------------

# To use: "make"
$(TARGET): rpicam-apps $(SOURCE)
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
	@pkg-config --exists boost || \
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
	@echo "  make				- Build (release)"
	@echo "  make debug			- Debug build"
	@echo "  make run			- Run the program"
	@echo "  make clean			- Remove local build artifacts"
	@echo "  make distclean		- Remove rpicam-apps build"
	@echo "  make check-deps	- Verify dependencies"

# -------------------------------------------------------------------------------------------
# Run program (w/ proper library path)
# -------------------------------------------------------------------------------------------

# To use: "make run"
run: $(TARGET)
	@echo "Running $(TARGET)..."
	LD_LIBRARY_PATH=$(RPICAM_BUILD_DIR) ./$(TARGET)

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

# -------------------------------------------------------------------------------------------
# Force rebuild everything (runs both clean, then build)
# -------------------------------------------------------------------------------------------

# To use: "make rebuild"
rebuild: clean all

# -------------------------------------------------------------------------------------------
# Phony targets (???)
# -------------------------------------------------------------------------------------------

# Defines all run conditions
# (i.e. what we can include after "make")
.PHONY: all rpicam-apps debug release run clean distclean rebuild check-deps help
