#!/usr/bin/env bash

#
# Upload the .tar.gz and .xml artifacts to Cloudsmith
#
# Repo-Auswahl (Variante 2, tag-basiert):
# - Tag enthält "alpha" → ALPHA-Repo
# - Tag enthält "beta"  → BETA-Repo
# - Tag enthält "rc"    → BETA-Repo
# - Tag ohne alpha/beta/rc (z. B. v1.0.0) → PROD-Repo
# - Kein Tag → ALPHA-Repo
#

set -xe

# Repos aus Secrets (GitHub Actions)
PROD_REPO=${CLOUDSMITH_STABLE_REPO:-'formifan2002/signalk-notes-opencpn-prod'}
BETA_REPO=${CLOUDSMITH_BETA_REPO:-'formifan2002/signalk-notes-opencpn-beta'}
ALPHA_REPO=${CLOUDSMITH_UNSTABLE_REPO:-'formifan2002/signalk-notes-opencpn-alpha'}

# Standard: GitHub Actions
LOCAL_BUILD=true
BUILD_DIR=./build
BUILD_ID=${GITHUB_RUN_NUMBER:-1}
PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}

# Tag aus GitHub-Ref ableiten (falls vorhanden)
BUILD_TAG=""
if [[ -n "$GITHUB_REF" && "$GITHUB_REF" == refs/tags/* ]]; then
  BUILD_TAG="${GITHUB_REF#refs/tags/}"
fi

# Wenn lokal (kein GITHUB_ACTIONS), Upload deaktivieren
if [ -n "$GITHUB_ACTIONS" ]; then
  LOCAL_BUILD=false
fi

set +x
if [ -z "$CLOUDSMITH_API_KEY" ] && [ "$LOCAL_BUILD" = "false" ]; then
  echo 'Cannot deploy to Cloudsmith, missing $CLOUDSMITH_API_KEY'
  exit 0
fi
set -x

commit=$(git rev-parse --short=7 HEAD) || commit="unknown"

ls -la
pwd

xml=$(ls "$BUILD_DIR"/*.xml)
cat "$xml"
tarball=$(ls "$BUILD_DIR"/*.tar.gz)
tarball_basename=${tarball##*/}
echo "$tarball"
echo "$tarball_basename"

# Versions-Infos aus pkg_version.sh (falls vorhanden)
if [ -f "$BUILD_DIR/pkg_version.sh" ]; then
  # Erwartet: setzt z. B. PKG_TARGET, PKG_TARGET_VERSION
  # shellcheck disable=SC1090
  source "$BUILD_DIR/pkg_version.sh"
else
  PKG_TARGET="unknown"
  PKG_TARGET_VERSION="0"
fi

if [ -n "${OCPN_TARGET}" ]; then
  tarball_name="signalk_notes_opencpn_pi-1.0.0.1-${PKG_TARGET}-arm64-${PKG_TARGET_VERSION}-${OCPN_TARGET}-tarball"
else
  tarball_name="signalk_notes_opencpn_pi-1.0.0.1-${PKG_TARGET}-arm64-${PKG_TARGET_VERSION}-tarball"
fi

if ls "$BUILD_DIR"/*.${PKG_EXT} >/dev/null 2>&1; then
  pkg=$(ls "$BUILD_DIR"/*.${PKG_EXT})
else
  pkg=""
fi

echo "BUILD_TAG: $BUILD_TAG"

# Version bestimmen
if [ -n "$BUILD_TAG" ]; then
  VERSION="$BUILD_TAG"
else
  VERSION="1.0.0.1+${BUILD_ID}.${commit}"
fi

# Repo nach Variante 2 auswählen
REPO="$ALPHA_REPO"
if [ -n "$BUILD_TAG" ]; then
  tag_lower=$(echo "$BUILD_TAG" | tr 'A-Z' 'a-z')
  if [[ "$tag_lower" == *alpha* ]]; then
    REPO="$ALPHA_REPO"
  elif [[ "$tag_lower" == *beta* ]] || [[ "$tag_lower" == *rc* ]]; then
    REPO="$BETA_REPO"
  else
    REPO="$PROD_REPO"
  fi
fi

echo "VERSION: $VERSION"
echo "TARGET REPO (masked): $REPO"
echo "TARGET REPO (b64): $(echo -n "$REPO" | base64)"

echo 'substituting xml file variables'
sed -i -e "s|--pkg_repo--|$REPO|"  "$xml"
sed -i -e "s|--name--|$tarball_name|" "$xml"
sed -i -e "s|--version--|$VERSION|" "$xml"
sed -i -e "s|--filename--|$tarball_basename|" "$xml"

cat "$xml"
ls -l "$BUILD_DIR"

cur_dir=$(pwd)
gunzip -f "$tarball"
cd "$BUILD_DIR"
rm -f metadata.xml
tarball_tar=$(ls *.tar)
xml_here=$(ls *.xml)
cp -f "$xml_here" metadata.xml

# Metadata in Tarball integrieren (wie im Template)
mkdir build_tar
cp "$tarball_tar" build_tar/.
cd build_tar
tar -xf "$tarball_tar"
rm *.tar
rm -rf root
cp ../metadata.xml .
tar -cf build_tarfile.tar *
tar -tf build_tarfile.tar
rm ../"$tarball_tar"
cp build_tarfile.tar ../"$tarball_tar"
cd ..
rm -rf build_tar

tar -tf "$tarball_tar"
gzip -f "$tarball_tar"
cd "$cur_dir"
ls -la "$BUILD_DIR"

# Upload nur in CI
if [ "$LOCAL_BUILD" = false ]; then
  # Metadata
  cloudsmith push raw --republish --no-wait-for-sync \
    --name "signalk_notes_opencpn_pi-1.0.0.1-${PKG_TARGET}-arm64-${PKG_TARGET_VERSION}-${OCPN_TARGET}-metadata" \
    --version "${VERSION}" \
    --summary "signalk_notes_opencpn OpenCPN plugin metadata for automatic installation" \
    "$REPO" "$xml"

  # Tarball
  cloudsmith push raw --republish --no-wait-for-sync \
    --name "$tarball_name" \
    --version "${VERSION}" \
    --summary "signalk_notes_opencpn OpenCPN plugin tarball for automatic installation" \
    "$REPO" "$tarball"

  # Optionales Paket (deb/exe/etc.)
  if [ "${PKG_EXT}" != "gz" ] && [ -n "$pkg" ]; then
    cloudsmith push raw --republish --no-wait-for-sync \
      --name "opencpn-package-signalk_notes_opencpn-1.0.0.1-${PKG_TARGET}-arm64-${PKG_TARGET_VERSION}-${OCPN_TARGET}.${PKG_EXT}" \
      --version "${VERSION}" \
      --summary "signalk_notes_opencpn .${PKG_EXT} installation package" \
      "$REPO" "$pkg"
  fi
fi
