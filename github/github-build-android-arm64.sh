#!/usr/bin/env bash
set -xeuo pipefail

#
# Build the Android artifacts inside the cimg/android container
#

echo "Working dir:"
pwd
ls -la

git submodule update --init opencpn-libs

# Optional: lokaler Cache-Modus (für CircleCI, stört unter GHA nicht)
CIRCLECI_LOCAL_LOWER="${CIRCLECI_LOCAL:-false}"
CIRCLECI_LOCAL_LOWER="${CIRCLECI_LOCAL_LOWER,,}"

if [[ "$CIRCLECI_LOCAL_LOWER" == "true" ]]; then
    if [[ -d ~/circleci-cache ]]; then
        if [[ -f ~/circleci-cache/apt-proxy ]]; then
            cat ~/circleci-cache/apt-proxy | sudo tee -a /etc/apt/apt.conf.d/00aptproxy
            cat /etc/apt/apt.conf.d/00aptproxy
        fi
        if [[ ! -f ~/circleci-cache/master.zip ]]; then
            sudo wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip -O ~/circleci-cache/master.zip
        fi
        MASTER_LOC=~/circleci-cache
    fi
else
    MASTER_LOC=$(pwd)
    wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip
fi

echo "unzipping $MASTER_LOC/master.zip"
unzip -qq -o "$MASTER_LOC/master.zip"

sudo apt-get -q update
sudo apt-get -y install git cmake gettext unzip python3-pip

# Extra build libs (im Container, sudo ist ok)
ME=$(echo "${0##*/}" | sed 's/\.sh//g')
EXTRA_LIBS=./ci/extras/extra_libs.txt
if [[ -f "$EXTRA_LIBS" ]]; then
    while read -r line; do
        [[ -z "$line" ]] && continue
        sudo apt-get -y install "$line"
    done < "$EXTRA_LIBS"
fi

EXTRA_LIBS=./ci/extras/${ME}_extra_libs.txt
if [[ -f "$EXTRA_LIBS" ]]; then
    while read -r line; do
        [[ -z "$line" ]] && continue
        sudo apt-get -y install "$line"
    done < "$EXTRA_LIBS"
fi

echo "After deps install:"
pwd
ls -la

mkdir -p build
cd build

rm -f CMakeCache.txt

# Neuere cmake via pip
python3 -m pip install --user --force-reinstall -q pip setuptools
sudo apt-get -y remove python3-six python3-colorama python3-urllib3 || true
export LC_ALL=C.UTF-8 LANG=C.UTF-8
python3 -m pip install --user -q cmake -vv
export PATH="$HOME/.local/bin:$PATH"

# NDK-Pfad an cimg/android anpassen
if ls -d /opt/android/sdk/ndk/* >/dev/null 2>&1; then
    last_ndk=$(ls -d /opt/android/sdk/ndk/* | tail -1)
elif ls -d /home/circleci/android-sdk/ndk/* >/dev/null 2>&1; then
    # Fallback für CircleCI-Umgebung
    last_ndk=$(ls -d /home/circleci/android-sdk/ndk/* | tail -1)
else
    echo "ERROR: Could not find Android NDK directory"
    exit 1
fi

test -d /opt/android || sudo mkdir -p /opt/android
sudo ln -sf "$last_ndk" /opt/android/ndk

# BUILD_TYPE bestimmen, falls nicht gesetzt
if [[ -z "${BUILD_TYPE:-}" ]]; then
    tag=$(git tag --contains HEAD || true)
    current_branch=$(git branch --show-current || true)
    if [[ -n "$tag" ]] || [[ "$current_branch" == "master" ]]; then
        BUILD_TYPE=Release
    else
        BUILD_TYPE=Debug
    fi
fi

echo "Using BUILD_TYPE=$BUILD_TYPE"

cmake -DCMAKE_TOOLCHAIN_FILE=cmake/android-aarch64-toolchain.cmake \
  -D_wx_selected_config=androideabi-qt-arm64 \
  -DwxQt_Build=build_android_release_64_static_O3 \
  -DQt_Build=build_arm64/qtbase \
  -DOCPN_Android_Common=OCPNAndroidCommon-master \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  ..

make VERBOSE=1
make package
