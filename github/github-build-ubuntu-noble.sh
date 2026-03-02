#!/usr/bin/env bash
set -xe

echo "=== Building OpenCPN Plugin for Ubuntu 24.04 (Noble) x86_64 ==="

# Install base tools
sudo apt-get update -qq
sudo apt-get install -y devscripts equivs git cmake g++ make pkg-config

# Install extra libs (same mechanism as Jammy)
ME=$(echo ${0##*/} | sed 's/\.sh//g')
EXTRA_LIBS=./ci/extras/extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    while read -r line; do
        sudo apt-get install -y $line
    done < "$EXTRA_LIBS"
fi
EXTRA_LIBS=./ci/extras/${ME}_extra_libs.txt
if test -f "$EXTRA_LIBS"; then
    while read -r line; do
        sudo apt-get install -y $line
    done < "$EXTRA_LIBS"
fi

# Install build dependencies from ci/control
sudo mk-build-deps --install ./ci/control

# wxWidgets GTK3
if [ -n "$BUILD_GTK3" ] && [ "$BUILD_GTK3" = "TRUE" ]; then
  sudo update-alternatives --set wx-config /usr/lib/*-linux-*/wx/config/gtk3-unicode-3.0
fi

# Prepare build
rm -rf build && mkdir build && cd build

tag=$(git tag --contains HEAD)
current_branch=$(git branch --show-current)

if [ -n "$tag" ] || [ "$current_branch" = "master" ]; then
  cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
else
  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr/local ..
fi

make -j$(nproc)
make package

ls -l

