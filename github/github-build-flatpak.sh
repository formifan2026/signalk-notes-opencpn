#!/usr/bin/env bash
set -xeuo pipefail

##############################################
# 0. CircleCI optional: apt proxy + cache
##############################################
if [[ "${CIRCLECI_LOCAL:-false}" == "true" ]]; then
    if [[ -d ~/circleci-cache ]]; then
        if [[ -f ~/circleci-cache/apt-proxy ]]; then
            cat ~/circleci-cache/apt-proxy | sudo tee -a /etc/apt/apt.conf.d/00aptproxy
        fi
    fi
fi

##############################################
# 1. Extra build libs (shared for both CI systems)
##############################################
ME=$(echo "${0##*/}" | sed 's/\.sh//g')

for EXTRA in ./ci/extras/extra_libs.txt ./ci/extras/${ME}_extra_libs.txt; do
    if [[ -f "$EXTRA" ]]; then
        sudo apt-get update
        while read -r line; do
            [[ -z "$line" ]] && continue
            sudo apt-get install -y "$line"
        done < "$EXTRA"
    fi
done

##############################################
# 2. Submodules
##############################################
git config --global protocol.file.allow always
git submodule update --init opencpn-libs

##############################################
# 3. Install Flatpak + Builder
#    GitHub Actions braucht zusätzliche Pakete
##############################################
if [[ -n "$CI" ]]; then
    sudo apt-get update
    sudo apt-get install --reinstall -y ca-certificates

    # GitHub Actions benötigt bubblewrap + --noninteractive
    if [[ "$GITHUB_ACTIONS" == "true" ]]; then
        sudo apt-get install -y flatpak flatpak-builder bubblewrap
    else
        # CircleCI Standard
        sudo apt-get install -y flatpak flatpak-builder
    fi
fi

##############################################
# 4. Add flathub remote
##############################################
flatpak remote-add --user --if-not-exists \
    flathub https://dl.flathub.org/repo/flathub.flatpakrepo

##############################################
# 5. Install SDK + OpenCPN runtime
##############################################
if [[ "$FLATPAK_BRANCH" == "beta" ]]; then
    flatpak install --user -y --noninteractive flathub org.freedesktop.Sdk//$SDK_VER
    flatpak remote-add --user --if-not-exists flathub-beta \
        https://dl.flathub.org/beta-repo/flathub-beta.flatpakrepo
    flatpak install --user -y --noninteractive flathub-beta org.opencpn.OpenCPN
else
    flatpak install --user -y --noninteractive flathub org.freedesktop.Sdk//$SDK_VER
    flatpak install --user -y --noninteractive flathub org.opencpn.OpenCPN
    FLATPAK_BRANCH="stable"
fi

##############################################
# 6. Build directory
##############################################
rm -rf build && mkdir build && cd build

if [[ -n "$WX_VER" ]]; then
    SET_WX_VER="-DWX_VER=$WX_VER"
else
    SET_WX_VER=""
fi

##############################################
# 7. CMake configure
##############################################
cmake \
  -DOCPN_TARGET="$OCPN_TARGET" \
  -DBUILD_ARCH="$BUILD_ARCH" \
  -DOCPN_FLATPAK_CONFIG=ON \
  -DSDK_VER="$SDK_VER" \
  -DFLATPAK_BRANCH="$FLATPAK_BRANCH" \
  $SET_WX_VER \
  ..

##############################################
# 8. Build + Package
##############################################
make flatpak-build
make flatpak-pkg
