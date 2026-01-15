Dependencies (Commands assuming default Raspbery Pi OS Bookworm):

sudo apt update
sudo apt install \
  build-essential \
  meson \
  ninja-build \
  pkg-config \
  libcamera-dev \
  libboost-all-dev \
  libjpeg-dev \
  libtiff5-dev \
  libpng-dev \
  libexif-dev \
  libdrm-dev

Note: libdrm-dev is still required for build (even with -Ddrm=disable in Meson) due to certain rpicam-apps scripts making it mandatory.
To verify dependencies, run "make check-deps" from project root.

To Build (assuming default Raspberry Pi OS Bookworm):
Clone Repo as git clone --recursive https://github.com/iandknoll/RPiCam-Finalcut.git . then run "make" from project root
-- For debug build: "make debug"
-- To clean build artifacts: "make clean"
-- To remove entire build: "make distclean"

To Run:
From project root, run "make run"
