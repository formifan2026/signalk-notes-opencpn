#!/usr/bin/env bash
set -xeuo pipefail

echo "Working dir:"
pwd
ls -la

git submodule update --init opencpn-libs

##############################################
# 1. OCPNAndroidCommon herunterladen
##############################################
if [[ "${CIRCLECI:-}" == "true" ]]; then
    # CircleCI: optionaler Cache
    if [[ "${CIRCLECI_LOCAL,,}" == "true" && -d ~/circleci-cache ]]; then
        if [[ -f ~/circleci-cache/apt-proxy ]]; then
            cat ~/circleci-cache/apt-proxy | sudo tee -a /etc/apt/apt.conf.d/00aptproxy
        fi
        if [[ ! -f ~/circleci-cache/master.zip ]]; then
            sudo wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip -O ~/circleci-cache/master.zip
        fi
        MASTER_LOC=~/circleci-cache
    else
        MASTER_LOC=$(pwd)
        wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip
    fi
else
    # GitHub Actions
    MASTER_LOC=$(pwd)
    wget https://github.com/bdbcat/OCPNAndroidCommon/archive/master.zip
fi

echo "unzipping $MASTER_LOC/master.zip"
unzip -qq -o "$MASTER_LOC/master.zip"

##############################################
# 2. Dependencies installieren
##############################################
sudo apt-get update
sudo apt-get install -y git cmake gettext unzip python3-pip

# Extra libs
ME=$(echo "${0##*/}" | sed 's/\.sh//g')
for EXTRA in ./ci/extras/extra_libs.txt ./ci/extras/${ME}_extra_libs.txt; do
    if [[ -f "$EXTRA" ]]; then
        while read -r line; do
            [[ -z "$line" ]] && continue
            sudo apt-get install -y "$line"
        done < "$EXTRA"
    fi
done

##############################################
# 3. Python/CMake Workarounds
##############################################
if [[ "${CIRCLECI:-}" == "true" ]]; then
    # Nur CircleCI braucht diese Workarounds
    python3 -m pip install --user --force-reinstall -q pip setuptools
    sudo apt-get remove -y python3-six python3-colorama python3-urllib3 || true
else
    # GitHub Actions: einfach aktualisieren
    python3 -m pip install --user --upgrade pip setuptools cmake
fi

export PATH="$HOME/.local/bin:$PATH"
export LC_ALL=C.UTF-8 LANG=C.UTF-8

##############################################
# 4. NDK-Pfad setzen
##############################################
if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
    # GitHub Actions: NDK r21e wurde in build.yml installiert
    export ANDROID_NDK_HOME="/opt/android-ndk"
    export ANDROID_NDK="/opt/android-ndk"
    export ANDROID_NDK_ROOT="/opt/android-ndk"
else
    # CircleCI: NDK aus Container verwenden
    last_ndk=$(ls -d /home/circleci/android-sdk/ndk/* | tail -1)
    sudo mkdir -p /opt/android
    sudo ln -sf "$last_ndk" /opt/android/ndk
    export ANDROID_NDK_HOME="/opt/android/ndk"
    export ANDROID_NDK="/opt/android/ndk"
    export ANDROID_NDK_ROOT="/opt/android/ndk"
fi

##############################################
# 5. Build vorbereiten
##############################################
mkdir -p build
cd build
rm -f CMakeCache.txt

# Build-Type bestimmen
if [[ -z "${BUILD_TYPE:-}" ]]; then
    tag=$(git tag --contains HEAD || true)
    branch=$(git branch --show-current || true)
    if [[ -n "$tag" || "$branch" == "master" ]]; then
        BUILD_TYPE=Release
    else
        BUILD_TYPE=Debug
    fi
fi

echo "Using BUILD_TYPE=$BUILD_TYPE"

##############################################
# 6. CMake + Build
##############################################
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/android-armhf-toolchain.cmake \
  -D_wx_selected_config=androideabi-qt-armhf \
  -DwxQt_Build=build_android_release_19_static_O3 \
  -DQt_Build=build_arm32_19_O3/qtbase \
  -DOCPN_Android_Common=OCPNAndroidCommon-master \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  ..

make VERBOSE=1
make package
