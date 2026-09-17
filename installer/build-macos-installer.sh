#!/usr/bin/env bash
#
# Packages an Amanorsac product or bundle into a macOS .pkg installer.
#
# This script CANNOT be run on Windows. It needs macOS, Xcode command line
# tools and a Release build produced by the Xcode generator. It runs on the
# macos job in .github/workflows/release-analog-bundle.yml, which is how this
# repository produces a mac installer.
#
# The package is NOT signed and NOT notarised. Gatekeeper will refuse to open
# it normally until a Developer ID Installer certificate and an app-specific
# notarisation credential are added (company P0 in the launch audit).
#
# Usage: ./installer/build-macos-installer.sh 1.0.0

set -euo pipefail

VERSION="${1:?usage: build-macos-installer.sh <version>}"
PRODUCT_NAME="${PRODUCT_NAME:-Amanorsac Analog Bundle}"
IDENTIFIER="${IDENTIFIER:-studio.amanorsac.analogbundle}"
BUILD_DIR="${BUILD_DIR:-build}"
# A customer installer must come from a build with licence enforcement armed.
armed_file="${BUILD_DIR}/licensing-armed.txt"
armed=$(cat "${armed_file}" 2>/dev/null || echo unknown)
if [[ "${armed}" != "1" && "${ALLOW_UNARMED:-0}" != "1" ]]; then
    echo "This build has licence enforcement OFF (licensing-armed.txt = ${armed})." >&2
    echo "Reconfigure with -DAMANORSAC_LICENSING=ON, or set ALLOW_UNARMED=1 for an internal build." >&2
    exit 1
fi

TARGETS=(A01 A02 A03 A04 A05 A06 A07 A08 A09 A10)

cd "$(dirname "$0")/.."
ROOT="$PWD"
SLUG="$(echo "$PRODUCT_NAME" | tr -cs '[:alnum:]' '_' | sed 's/_*$//')"
STAGE="$ROOT/installer/stage/macos"

rm -rf "$STAGE"
mkdir -p "$STAGE/VST3" "$STAGE/Components" "$STAGE/Applications"

# ------------------------------------------------------------------ stage
for id in "${TARGETS[@]}"; do
  artefacts="$BUILD_DIR/Amanorsac${id}_artefacts/Release"
  [ -d "$artefacts/VST3" ] && cp -R "$artefacts/VST3/"*.vst3 "$STAGE/VST3/" 2>/dev/null || true
  [ -d "$artefacts/AU" ] && cp -R "$artefacts/AU/"*.component "$STAGE/Components/" 2>/dev/null || true
  [ -d "$artefacts/Standalone" ] && cp -R "$artefacts/Standalone/"*.app "$STAGE/Applications/" 2>/dev/null || true
done

vst3_count=$(find "$STAGE/VST3" -maxdepth 1 -name '*.vst3' | wc -l | tr -d ' ')
au_count=$(find "$STAGE/Components" -maxdepth 1 -name '*.component' | wc -l | tr -d ' ')
app_count=$(find "$STAGE/Applications" -maxdepth 1 -name '*.app' | wc -l | tr -d ' ')
echo "Payload: ${vst3_count} VST3, ${au_count} AU, ${app_count} apps"

if [ "$vst3_count" -ne "${#TARGETS[@]}" ]; then
  echo "Expected ${#TARGETS[@]} VST3 bundles, found ${vst3_count}. Refusing to ship a partial bundle." >&2
  exit 1
fi

# ------------------------------------------------- one component package each
PKGROOT="$ROOT/installer/stage/pkgs"
rm -rf "$PKGROOT"; mkdir -p "$PKGROOT"

pkgbuild --root "$STAGE/VST3" \
         --identifier "${IDENTIFIER}.vst3" \
         --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         "$PKGROOT/vst3.pkg"

if [ "$au_count" -gt 0 ]; then
  pkgbuild --root "$STAGE/Components" \
           --identifier "${IDENTIFIER}.au" \
           --version "$VERSION" \
           --install-location "/Library/Audio/Plug-Ins/Components" \
           "$PKGROOT/au.pkg"
fi

if [ "$app_count" -gt 0 ]; then
  pkgbuild --root "$STAGE/Applications" \
           --identifier "${IDENTIFIER}.app" \
           --version "$VERSION" \
           --install-location "/Applications/Amanorsac Studio" \
           "$PKGROOT/app.pkg"
fi

# ------------------------------------------------------------- distribution
cat > "$PKGROOT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${PRODUCT_NAME}</title>
    <organization>studio.amanorsac</organization>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <choices-outline>
        <line choice="vst3"/>
$([ "$au_count" -gt 0 ] && echo '        <line choice="au"/>')
$([ "$app_count" -gt 0 ] && echo '        <line choice="app"/>')
    </choices-outline>
    <choice id="vst3" title="VST3 plug-ins" description="Installs to /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="${IDENTIFIER}.vst3"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.vst3" version="${VERSION}">vst3.pkg</pkg-ref>
$([ "$au_count" -gt 0 ] && cat <<AU
    <choice id="au" title="Audio Units" description="Installs to /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="${IDENTIFIER}.au"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.au" version="${VERSION}">au.pkg</pkg-ref>
AU
)
$([ "$app_count" -gt 0 ] && cat <<APP
    <choice id="app" title="Standalone applications" description="Installs to /Applications/Amanorsac Studio">
        <pkg-ref id="${IDENTIFIER}.app"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.app" version="${VERSION}">app.pkg</pkg-ref>
APP
)
</installer-gui-script>
XML

mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/${SLUG}_${VERSION}_macOS.pkg"
UNSIGNED="$PKGROOT/unsigned.pkg"

productbuild --distribution "$PKGROOT/distribution.xml" \
             --package-path "$PKGROOT" \
             --version "$VERSION" \
             "$UNSIGNED"

# Sign the installer when a Developer ID Installer identity is in the keychain.
# Gatekeeper refuses an unsigned package and notarisation cannot proceed
# without one, so this is not optional for release; a local packaging run
# without the certificates still produces something testable.
INSTALLER_IDENTITY="$(security find-identity -v 2>/dev/null | sed -n 's/.*"\(Developer ID Installer: [^"]*\)".*/\1/p' | head -1)"
if [ -n "$INSTALLER_IDENTITY" ]; then
  echo "Signing with: $INSTALLER_IDENTITY"
  productsign --sign "$INSTALLER_IDENTITY" "$UNSIGNED" "$OUT"
  pkgutil --check-signature "$OUT"
  SIGNED="yes"
else
  echo "No Developer ID Installer identity in the keychain; leaving the package unsigned." >&2
  cp "$UNSIGNED" "$OUT"
  SIGNED="no"
fi

echo ""
echo "Installer: $OUT"
echo "Signed:    $SIGNED"
if [ "$SIGNED" = "no" ]; then
  echo "Gatekeeper will refuse an unsigned package, and it cannot be notarised."
fi
