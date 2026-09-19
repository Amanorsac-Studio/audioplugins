#!/usr/bin/env bash
#
# Packages an Amanorsac product or bundle into one macOS .pkg, to the Amanorsac
# Studio Installer & Packaging Standard:
#
#   - one .pkg, nothing to unpack                                   (P1, P2)
#   - each format its own checkbox, with its full path              (P11, P12)
#   - the standard system folders                                   (3.2)
#   - Welcome, Licence, Read Me and Done pages                      (section 5)
#   - an uninstall script, because a .pkg leaves nothing to run     (P24)
#   - <Product>-<version>-macOS.pkg                                 (P28)
#
# This script CANNOT be run on Windows. It needs macOS, Xcode command line
# tools and a Release build from the Xcode generator. It runs on the macos job
# of the release workflows in .github/workflows.
#
# It signs the package when a Developer ID Installer identity is in the
# keychain; notarisation and stapling happen in the workflow afterwards.
#
# Usage: ./installer/build-macos-installer.sh 1.0.0
#        PRODUCT_NAME="Amanorsac Digital Bundle" TARGET_IDS="D01 D02 ..." ./installer/build-macos-installer.sh 1.0.0

set -euo pipefail

VERSION="${1:?usage: build-macos-installer.sh <version>}"
PRODUCT_NAME="${PRODUCT_NAME:-Amanorsac Analog Bundle}"
IDENTIFIER="${IDENTIFIER:-studio.amanorsac.analogbundle}"
BUILD_DIR="${BUILD_DIR:-build}"
LICENSED="${LICENSED:-1}"

# A customer installer must come from a build with licence enforcement armed.
armed_file="${BUILD_DIR}/licensing-armed.txt"
armed=$(cat "${armed_file}" 2>/dev/null || echo unknown)
if [[ "${LICENSED}" == "1" && "${armed}" != "1" && "${ALLOW_UNARMED:-0}" != "1" ]]; then
    echo "This build has licence enforcement OFF (licensing-armed.txt = ${armed})." >&2
    echo "Reconfigure with -DAMANORSAC_LICENSING=ON, or set ALLOW_UNARMED=1 for an internal build." >&2
    exit 1
fi

# Which plug-ins this bundle holds. TARGET_IDS overrides the analog default.
read -r -a TARGETS <<< "${TARGET_IDS:-A01 A02 A03 A04 A05 A06 A07 A08 A09 A10}"

case "$PRODUCT_NAME" in
  *Digital*) ONE_LINE="${ONE_LINE:-Ten precision processors that show you exactly what they are doing to your sound.}" ;;
  *)         ONE_LINE="${ONE_LINE:-Ten analog processors: preamps, equalisers, compressors, tape and plate, built from the ground up.}" ;;
esac

cd "$(dirname "$0")/.."
ROOT="$PWD"
# P28: the product name as the studio writes it, spaces removed.
FILE_STEM="$(echo "$PRODUCT_NAME" | tr -d ' ')-${VERSION}-macOS"
STAGE="$ROOT/installer/stage/macos"
SUPPORT_DIR="/Library/Application Support/Amanorsac Studio/${PRODUCT_NAME}"

rm -rf "$STAGE"
mkdir -p "$STAGE/VST3" "$STAGE/Components" "$STAGE/Applications" "$STAGE/Support"

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

# ------------------------------------------------------------------ README and uninstaller
if [ "$LICENSED" = "1" ]; then
  ACTIVATION_TEXT="ACTIVATION
Open any plug-in and enter the licence key from your account at
amanorsac.studio/my-apps. One key unlocks the whole bundle on this computer
and covers two computers."
  NEXT_HTML="Open any of the plug-ins and enter your licence key once. It is in your account under <b>My Apps</b> at amanorsac.studio."
  LICENCE_HTML="<h3>This product needs a licence key</h3><p>The first plug-in you open will ask for your key, once. It unlocks the whole bundle on this Mac. Your key is in your account under <b>My Apps</b> at amanorsac.studio, and in Amanorsac Hub. One key covers two computers.</p>"
else
  ACTIVATION_TEXT="NO ACTIVATION
There is no key and no account. It never connects to the internet."
  NEXT_HTML="Open your DAW. There is no key to enter."
  LICENCE_HTML="<h3>No account, no activation</h3><p>There is no key and no account. This product never connects to the internet.</p>"
fi

cat > "$STAGE/Support/README.txt" <<README
$(echo "$PRODUCT_NAME" | tr '[:lower:]' '[:upper:]') ${VERSION}
Amanorsac Studio · amanorsac.studio

WHAT THIS IS
${ONE_LINE}

INSTALLING
macOS     Open ${FILE_STEM}.pkg and follow the installer.

WHAT GETS INSTALLED
  VST3        /Library/Audio/Plug-Ins/VST3/<Plug-in>.vst3
  Audio Unit  /Library/Audio/Plug-Ins/Components/<Plug-in>.component
  Standalone  /Applications/Amanorsac Studio/<Plug-in>.app
The installer lets you choose which of these to install. Press Customize.

YOUR PRESETS AND SETTINGS
  macOS     ~/Library/Application Support/Amanorsac Studio/Presets/<Plug-in>/
Uninstalling does not delete these.

${ACTIVATION_TEXT}

IF YOUR DAW DOES NOT SEE IT
1. Rescan plug-ins in your DAW's preferences.
2. Check the folders above are in your DAW's plug-in scan paths.
3. Restart the DAW. Logic may need a restart of the Mac for new Audio Units.
Still missing? hello@amanorsac.studio

UNINSTALLING
macOS     Run "Uninstall ${PRODUCT_NAME}.command" in
          ${SUPPORT_DIR}/
          or delete the files listed above.

LICENCE AND PRIVACY
amanorsac.studio/legal · amanorsac.studio/privacy

SUPPORT
hello@amanorsac.studio
README

UNINSTALLER="$STAGE/Support/Uninstall ${PRODUCT_NAME}.command"
{
  echo '#!/bin/bash'
  echo "# Removes ${PRODUCT_NAME}: exactly the files its installer placed, nothing else."
  echo "# Your own presets and settings are kept."
  echo 'echo "This removes '"${PRODUCT_NAME}"'. Your presets and settings are kept."'
  echo 'read -r -p "Continue? [y/N] " answer'
  echo '[[ "$answer" =~ ^[Yy]$ ]] || exit 0'
  echo 'echo "macOS will ask for your password, because these folders are shared by every user."'
  for bundle in "$STAGE/VST3/"*.vst3; do [ -e "$bundle" ] && echo "sudo rm -rf \"/Library/Audio/Plug-Ins/VST3/$(basename "$bundle")\""; done
  for bundle in "$STAGE/Components/"*.component; do [ -e "$bundle" ] && echo "sudo rm -rf \"/Library/Audio/Plug-Ins/Components/$(basename "$bundle")\""; done
  for bundle in "$STAGE/Applications/"*.app; do [ -e "$bundle" ] && echo "sudo rm -rf \"/Applications/Amanorsac Studio/$(basename "$bundle")\""; done
  echo 'sudo rmdir "/Applications/Amanorsac Studio" 2>/dev/null || true'
  echo "for id in vst3 au app support; do sudo pkgutil --forget \"${IDENTIFIER}.\$id\" >/dev/null 2>&1 || true; done"
  echo "sudo rm -rf \"${SUPPORT_DIR}\""
  echo 'sudo rmdir "/Library/Application Support/Amanorsac Studio" 2>/dev/null || true'
  echo "echo \"${PRODUCT_NAME} was removed.\""
} > "$UNINSTALLER"
chmod +x "$UNINSTALLER"

# ------------------------------------------------- one component package each
PKGROOT="$ROOT/installer/stage/pkgs"
rm -rf "$PKGROOT"; mkdir -p "$PKGROOT"

pkgbuild --root "$STAGE/VST3" --identifier "${IDENTIFIER}.vst3" --version "$VERSION" \
         --install-location "/Library/Audio/Plug-Ins/VST3" "$PKGROOT/vst3.pkg"

if [ "$au_count" -gt 0 ]; then
  pkgbuild --root "$STAGE/Components" --identifier "${IDENTIFIER}.au" --version "$VERSION" \
           --install-location "/Library/Audio/Plug-Ins/Components" "$PKGROOT/au.pkg"
fi

if [ "$app_count" -gt 0 ]; then
  pkgbuild --root "$STAGE/Applications" --identifier "${IDENTIFIER}.app" --version "$VERSION" \
           --install-location "/Applications/Amanorsac Studio" "$PKGROOT/app.pkg"
fi

# The README and the uninstaller always install: they are how the product is removed.
pkgbuild --root "$STAGE/Support" --identifier "${IDENTIFIER}.support" --version "$VERSION" \
         --install-location "$SUPPORT_DIR" "$PKGROOT/support.pkg"

# ------------------------------------------------------------- installer pages
RESOURCES="$PKGROOT/resources"
mkdir -p "$RESOURCES"
cp "$STAGE/Support/README.txt" "$RESOURCES/README.txt"
# The logo is light artwork, so it is only used where the installer is dark.
cp "$ROOT/assets/brand/AmanorsacLogo.png" "$RESOURCES/background-dark.png"

STYLE="<style>body{font-family:Inter,-apple-system,Helvetica Neue,sans-serif;font-size:13px;line-height:1.5}h2{font-size:20px;margin:0 0 4px}h3{font-size:14px;margin:14px 0 4px}p{margin:0 0 10px}.dim{opacity:.65}</style>"

cat > "$RESOURCES/welcome.html" <<HTML
<html><head><meta charset="utf-8">${STYLE}</head><body>
<h2>${PRODUCT_NAME}</h2>
<p class="dim">Version ${VERSION} &middot; Amanorsac Studio</p>
<p>${ONE_LINE}</p>
<p>This installs ${vst3_count} plug-ins. Press <b>Customize</b> on the next pages to choose which formats to install; each one shows exactly where it goes.</p>
<p class="dim">Nothing else is installed: no toolbars, no extras, no background services.</p>
</body></html>
HTML

cat > "$RESOURCES/license.html" <<HTML
<html><head><meta charset="utf-8">${STYLE}</head><body>
<h2>Licence</h2>
<p>By installing this software you agree to the Amanorsac Studio End User Licence Agreement. The full, current text is published at <a href="https://amanorsac.studio/legal">amanorsac.studio/legal</a>.</p>
<p>How we handle your data: <a href="https://amanorsac.studio/privacy">amanorsac.studio/privacy</a>.</p>
${LICENCE_HTML}
<p class="dim">macOS will ask for your password once, because plug-in folders are shared by every user of this Mac. The installer asks for nothing else.</p>
</body></html>
HTML

cat > "$RESOURCES/conclusion.html" <<HTML
<html><head><meta charset="utf-8">${STYLE}</head><body>
<h2>${PRODUCT_NAME} is installed</h2>
<h3>What was installed, and where</h3>
<p>VST3 &nbsp; <code>/Library/Audio/Plug-Ins/VST3/</code><br>
Audio Units &nbsp; <code>/Library/Audio/Plug-Ins/Components/</code><br>
Applications &nbsp; <code>/Applications/Amanorsac Studio/</code><br>
Read Me and uninstaller &nbsp; <code>${SUPPORT_DIR}/</code></p>
<p class="dim">Only the formats you selected were installed.</p>
<h3>What next</h3>
<p>${NEXT_HTML}</p>
<p>If your DAW does not list the plug-ins, run a plug-in rescan in its preferences.</p>
</body></html>
HTML

# ------------------------------------------------------------- distribution
cat > "$PKGROOT/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${PRODUCT_NAME}</title>
    <organization>studio.amanorsac</organization>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <background-darkAqua file="background-dark.png" alignment="bottomleft" scaling="none"/>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="license.html" mime-type="text/html"/>
    <readme file="README.txt" mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <choices-outline>
        <line choice="vst3"/>
$([ "$au_count" -gt 0 ] && echo '        <line choice="au"/>')
$([ "$app_count" -gt 0 ] && echo '        <line choice="app"/>')
        <line choice="support"/>
    </choices-outline>
    <choice id="vst3" title="VST3 plug-ins (${vst3_count})" description="Installs to /Library/Audio/Plug-Ins/VST3/ &#8212; the standard folder every DAW scans.">
        <pkg-ref id="${IDENTIFIER}.vst3"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.vst3" version="${VERSION}">vst3.pkg</pkg-ref>
$([ "$au_count" -gt 0 ] && cat <<AU
    <choice id="au" title="Audio Units (${au_count})" description="Installs to /Library/Audio/Plug-Ins/Components/ &#8212; for Logic Pro, GarageBand and other Audio Unit hosts.">
        <pkg-ref id="${IDENTIFIER}.au"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.au" version="${VERSION}">au.pkg</pkg-ref>
AU
)
$([ "$app_count" -gt 0 ] && cat <<APP
    <choice id="app" title="Standalone applications (${app_count})" description="Installs to /Applications/Amanorsac Studio/">
        <pkg-ref id="${IDENTIFIER}.app"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.app" version="${VERSION}">app.pkg</pkg-ref>
APP
)
    <choice id="support" title="Read Me and uninstaller" description="Installs to ${SUPPORT_DIR}/ &#8212; always installed, because it is how the product is removed." enabled="false" selected="true">
        <pkg-ref id="${IDENTIFIER}.support"/>
    </choice>
    <pkg-ref id="${IDENTIFIER}.support" version="${VERSION}">support.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/${FILE_STEM}.pkg"
UNSIGNED="$PKGROOT/unsigned.pkg"

productbuild --distribution "$PKGROOT/distribution.xml" \
             --package-path "$PKGROOT" \
             --resources "$RESOURCES" \
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
echo "Bytes:     $(stat -f%z "$OUT" 2>/dev/null || wc -c < "$OUT")"
echo "Signed:    $SIGNED"
if [ "$SIGNED" = "no" ]; then
  echo "Gatekeeper will refuse an unsigned package, and it cannot be notarised."
fi
