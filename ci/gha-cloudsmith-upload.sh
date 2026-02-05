#!/usr/bin/env bash
set -e

echo "=== Cloudsmith Upload (GHA) ==="
echo "PWD: $(pwd)"
ls -la

# ---------------------------------------------------------
# 1. Repositories aus GitHub Secrets
# ---------------------------------------------------------
PROD_REPO="$CLOUDSMITH_STABLE_REPO"
BETA_REPO="$CLOUDSMITH_BETA_REPO"
ALPHA_REPO="$CLOUDSMITH_UNSTABLE_REPO"

# ---------------------------------------------------------
# 2. Build-Informationen aus GitHub Actions
# ---------------------------------------------------------
BUILD_DIR="."
BUILD_ID="${GITHUB_RUN_NUMBER:-1}"
BUILD_BRANCH="${GITHUB_REF_NAME}"
commit=$(git rev-parse --short=7 HEAD || echo "unknown")

if [[ "$GITHUB_REF" == refs/tags/* ]]; then
    BUILD_TAG="${GITHUB_REF#refs/tags/}"
else
    BUILD_TAG=""
fi

echo "BUILD_BRANCH: $BUILD_BRANCH"
echo "BUILD_TAG:    $BUILD_TAG"
echo "COMMIT:       $commit"
echo "RUN:          $BUILD_ID"

# ---------------------------------------------------------
# 3. Version bestimmen
# ---------------------------------------------------------
if [ -n "$BUILD_TAG" ]; then
    VERSION="$BUILD_TAG"
else
    VERSION="1.0.0.1+${BUILD_ID}.${commit}"
fi

# ---------------------------------------------------------
# 4. Repo bestimmen (PROD/BETA/ALPHA)
# ---------------------------------------------------------
BRANCH_LOWER=$(echo "$BUILD_BRANCH" | tr 'A-Z' 'a-z')

if [ "$BRANCH_LOWER" = "master" ] || [ "$BRANCH_LOWER" = "main" ]; then
    if [ -n "$BUILD_TAG" ]; then
        REPO="$PROD_REPO"
    else
        REPO="$BETA_REPO"
    fi
else
    if [ -n "$BUILD_TAG" ]; then
        REPO="$BETA_REPO"
    else
        REPO="$ALPHA_REPO"
    fi
fi

echo "VERSION:     $VERSION"
echo "TARGET REPO: $REPO"

# ---------------------------------------------------------
# 5. API-Key prüfen
# ---------------------------------------------------------
if [ -z "$CLOUDSMITH_API_KEY" ]; then
    echo "ERROR: CLOUDSMITH_API_KEY fehlt – Upload abgebrochen."
    exit 1
fi

# ---------------------------------------------------------
# 6. Artefakte finden
# ---------------------------------------------------------
tarball=$(ls *.tar.gz 2>/dev/null | head -n1 || true)
xml=$(ls *.xml 2>/dev/null | head -n1 || true)
pkg=$(ls *.deb *.apk *.flatpak *.zip 2>/dev/null | head -n1 || true)

echo "Tarball:  ${tarball:-<none>}"
echo "XML:      ${xml:-<none>}"
echo "Package:  ${pkg:-<none>}"

if [ -z "$tarball" ] || [ -z "$xml" ]; then
    echo "ERROR: Tarball oder XML nicht gefunden – nichts zu uploaden."
    exit 1
fi

# ---------------------------------------------------------
# 7. Upload zu Cloudsmith
# ---------------------------------------------------------
echo "Uploading metadata XML..."
cloudsmith push raw --republish --no-wait-for-sync \
    --name "signalk_notes_opencpn_metadata" \
    --version "$VERSION" \
    "$REPO" "$xml"

echo "Uploading tarball..."
cloudsmith push raw --republish --no-wait-for-sync \
    --name "signalk_notes_opencpn_tarball" \
    --version "$VERSION" \
    "$REPO" "$tarball"

if [ -n "$pkg" ]; then
    echo "Uploading package: $pkg"
    cloudsmith push raw --republish --no-wait-for-sync \
        --name "signalk_notes_opencpn_pkg" \
        --version "$VERSION" \
        "$REPO" "$pkg"
fi

echo "=== Cloudsmith Upload complete ==="
