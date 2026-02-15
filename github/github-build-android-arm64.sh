#!/usr/bin/env bash
set -euo pipefail

# ------------------------------------------------------------
# Universelles Android-Build-Script für:
# - GitHub Actions
# - CircleCI
# - Travis CI
# - AppVeyor (MSYS2/MinGW)
# ------------------------------------------------------------

echo "=== Working directory ==="
pwd
ls -la

# ------------------------------------------------------------
# Git Submodules
# ------------------------------------------------------------
git submodule update --init opencpn-libs

# ------------------------------------------------------------
# CI-Erkennung (ohne Build-Logik zu verändern)
# ------------------------------------------------------------
CI_SYSTEM="unknown"

if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
    CI_SYSTEM="github"
elif [[ "${CIRCLECI:-}" == "true" ]]; then
    CI_SYSTEM="circleci"
elif [[ "${TRAVIS:-}" == "true" ]]; then
    CI_SYSTEM="travis"
elif [[ "${APPVEYOR:-}" == "True" ]]; then
    CI_SYSTEM="appveyor"
fi

echo "Detected CI system: $CI_SYSTEM"

# ------------------------------------------------------------
# master.zip Download / Cache
# ------------------------------------------------------------
MASTER_LOC=$(pwd)

if [[ "${CIRCLECI_LOCAL:-false}" =~ ^([Tt][Rr][Uu][Ee])$ ]]; then
    if [[ -d ~/circleci-cache ]]; then
        if [[ -f ~/circleci-cache/apt-proxy ]]; then
            sudo tee -a /etc/apt/apt.conf.d/00aptproxy < ~/circleci-cache/apt-proxy
        fi
        if [[ ! -f ~/circleci-cache/master.zip ]]; then
            sudo wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip \
                -O ~/circleci-cache/master.zip
        fi
        MASTER_LOC=~/circleci-cache
    fi
else
    wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip
fi

echo "Unzipping $MASTER_LOC/master.zip"
unzip -qq -o "$MASTER_LOC/master.zip"

# ------------------------------------------------------------
# Dependency Installation (plattformübergreifend)
# ------------------------------------------------------------
case "$CI_SYSTEM" in
    github|circleci|travis)
        sudo apt-get -q update
        sudo apt-get -y install git cmake gettext unzip python3-pip
        ;;
    appveyor)
        # AppVeyor Windows → MSYS2/MinGW
        pacman -Sy --noconfirm git cmake unzip python-pip
        ;;
esac

# ------------------------------------------------------------
# Extra Build Libs
# ------------------------------------------------------------
ME=$(basename "$0" .sh)

for EXTRA in "./ci/extras/extra_libs.txt" "./ci/extras/${ME}_extra_libs.txt"; do
    if [[ -f "$EXTRA" ]]; then
        while read -r pkg; do
            [[ -z "$pkg" ]] && continue
            case "$CI_SYSTEM" in
                github|circleci|travis)
                    sudo apt-get -y install "$pkg"
                    ;;
                appveyor)
                    pacman -Sy --noconfirm "$pkg"
                    ;;
            esac
        done < "$EXTRA"
    fi
done

echo "After deps install:"
pwd
ls -la

# ------------------------------------------------------------
# Build directory
# ------------------------------------------------------------
mkdir -p build
cd build
rm -f CMakeCache.txt

# ------------------------------------------------------------
# Install modern CMake via pip
# ------------------------------------------------------------
python3 -m pip install --user --force-reinstall -q pip setuptools
sudo apt-get -y remove python3-six python3-colorama python3-urllib3 || true
export LC_ALL=C.UTF-8 LANG=C.UTF-8
python3 -m pip install --user -q cmake -vv
export PATH="$HOME/.local/bin:$PATH"

# ------------------------------------------------------------
# NDK detection (universell)
# ------------------------------------------------------------
NDK_CANDIDATES=(
    "/opt/android/sdk/ndk"
    "/home/circleci/android-sdk/ndk"
    "/c/Android/ndk"          # AppVeyor Windows
    "/mingw64/android-ndk"    # MSYS2
)

last_ndk=""

for base in "${NDK_CANDIDATES[@]}"; do
    if ls -d "$base"/* >/dev/null 2>&1; then
        last_ndk=$(ls -d "$base"/* | tail -1)
        break
    fi
done

if [[ -z "$last_ndk" ]]; then
    echo "ERROR: Could not find Android NDK directory"
    exit 1
fi

test -d /opt/android || sudo mkdir -p /opt/android
sudo ln -sf "$last_ndk" /opt/android/ndk

echo "Using NDK: $last_ndk"

# ------------------------------------------------------------
# BUILD_TYPE (NICHT verändert!)
# ------------------------------------------------------------
echo "Using BUILD_TYPE=${BUILD_TYPE:-<unset>}"

# ------------------------------------------------------------
# CMake Build (NICHT verändert!)
# ------------------------------------------------------------
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/android-aarch64-toolchain.cmake \
  -D_wx_selected_config=androideabi-qt-arm64 \
  -DwxQt_Build=build_android_release_64_static_O3 \
  -DQt_Build=build_arm64/qtbase \
  -DOCPN_Android_Common=OCPNAndroidCommon-master \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  ..

make VERBOSE=1
make package
