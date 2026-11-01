#!/usr/bin/env bash
# ESP-IDF v5.5.3 setup for Linux / macOS. Target: esp32p4.
# Usage: bash setup_idf_unix.sh
set -euo pipefail
IDF_VER=v5.5.3
IDF_ROOT="$HOME/esp"
IDF_PATH_DIR="$IDF_ROOT/esp-idf-$IDF_VER"

if [ "$(uname)" = "Linux" ] && command -v apt-get >/dev/null; then
  sudo apt-get update
  sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv \
    cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
  # Serial port access without sudo (takes effect after re-login).
  sudo usermod -aG dialout "$USER" || true
elif [ "$(uname)" = "Darwin" ]; then
  command -v brew >/dev/null || { echo "Install Homebrew first: https://brew.sh"; exit 1; }
  brew install cmake ninja dfu-util ccache python3
fi

mkdir -p "$IDF_ROOT"
if [ ! -d "$IDF_PATH_DIR" ]; then
  git clone -b "$IDF_VER" --recursive --depth 1 --shallow-submodules \
    https://github.com/espressif/esp-idf.git "$IDF_PATH_DIR"
fi
cd "$IDF_PATH_DIR"
./install.sh esp32p4

echo
echo "Done. In every new terminal run:"
echo "    . $IDF_PATH_DIR/export.sh"
echo "Then check:  idf.py --version"
