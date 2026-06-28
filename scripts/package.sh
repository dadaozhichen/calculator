#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════════
#  Package Calculator for distribution
#  Usage:  ./scripts/package.sh              # build + package
#          ./scripts/package.sh --skip-build # only package existing binary
# ═══════════════════════════════════════════════════════════════════════════════

set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

APP_NAME="Calculator"
VERSION="1.0.0"
BUILD_DIR="build/dist"
SKIP_BUILD=false

for arg in "$@"; do
    [ "$arg" = "--skip-build" ] && SKIP_BUILD=true
done

# ── Build ──────────────────────────────────────────────────────────────────
if [ "$SKIP_BUILD" = false ]; then
    echo ">> Building release binary…"
    xmake f -m release  > /dev/null
    xmake               > /dev/null
fi

BINARY="$(find build -type f \( -name calculator -o -name calculator.exe \) | head -1 2>/dev/null || true)"
if [ -z "$BINARY" ] || [ ! -f "$BINARY" ]; then
    echo "!! Cannot find built binary. Build first or pass --skip-build."
    exit 1
fi
echo ">> Using binary: $BINARY"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# ════════════════════════════════════════════════════════════════════════════
#  macOS  —  .app bundle → .dmg
# ════════════════════════════════════════════════════════════════════════════
if [ "$(uname)" = "Darwin" ]; then
    echo ">> Creating macOS .app bundle…"

    APP_DIR="$BUILD_DIR/$APP_NAME.app"
    CONTENTS="$APP_DIR/Contents"
    MACOS_DIR="$CONTENTS/MacOS"
    RES_DIR="$CONTENTS/Resources"

    mkdir -p "$MACOS_DIR" "$RES_DIR"
    cp "$BINARY" "$MACOS_DIR/$APP_NAME"
    cp "scripts/Info.plist" "$CONTENTS/"

    # ── Sign the binary (ad-hoc) so it works on Apple Silicon ─────────────
    codesign --force --deep --sign - "$APP_DIR" 2>/dev/null || true

    echo ">> Bundle created: $APP_DIR"

    # ── DMG ──────────────────────────────────────────────────────────────
    DMG_PATH="$BUILD_DIR/$APP_NAME-$VERSION-macos.dmg"

    if command -v create-dmg &>/dev/null; then
        echo ">> Creating DMG with create-dmg…"
        create-dmg \
            --volname "$APP_NAME" \
            --window-pos 200 120 \
            --window-size 600 400 \
            --icon-size 100 \
            --icon "$APP_NAME.app" 175 190 \
            --hide-extension "$APP_NAME.app" \
            --app-drop-link 425 190 \
            "$DMG_PATH" \
            "$APP_DIR"
        echo ">> DMG: $DMG_PATH"
    else
        echo ">> create-dmg not found — using hdiutil instead."
        hdiutil create -volname "$APP_NAME" \
            -srcfolder "$APP_DIR" \
            -ov -format UDZO \
            "$DMG_PATH"
        echo ">> DMG: $DMG_PATH"
    fi

    # Zip fallback for CI artifact.
    (cd "$BUILD_DIR" && zip -r "$APP_NAME-$VERSION-macos.zip" "$APP_NAME.app" > /dev/null)

# ════════════════════════════════════════════════════════════════════════════
#  Windows  —  NSIS installer
# ════════════════════════════════════════════════════════════════════════════
elif [ "$(uname -o 2>/dev/null || true)" = "Msys" ] || \
     [ -n "${WINDIR:-}" ] || \
     [ "${OSTYPE:-}" = "msys" ] || \
     uname -a | grep -qiE "mingw|msys|cygwin" 2>/dev/null; then
    echo ">> Packaging Windows binary…"

    EXE_NAME="$APP_NAME.exe"
    cp "$BINARY" "$BUILD_DIR/$EXE_NAME"

    # ── NSIS installer ───────────────────────────────────────────────────
    if command -v makensis &>/dev/null; then
        echo ">> Creating NSIS installer…"
        makensis -DVERSION="$VERSION" \
                 -DAPP_NAME="$APP_NAME" \
                 -DOUTFILE="$BUILD_DIR/$APP_NAME-$VERSION-windows-installer.exe" \
                 -DSRCDIR="$BUILD_DIR" \
                 scripts/installer.nsi
        echo ">> Installer: $BUILD_DIR/$APP_NAME-$VERSION-windows-installer.exe"
    else
        echo "!! makensis (NSIS) not found — falling back to ZIP."
    fi

    # Zip fallback.
    (cd "$BUILD_DIR" && zip "$APP_NAME-$VERSION-windows.zip" "$EXE_NAME" > /dev/null)
    echo ">> Zip: $BUILD_DIR/$APP_NAME-$VERSION-windows.zip"

# ════════════════════════════════════════════════════════════════════════════
#  Linux  —  .tar.gz
# ════════════════════════════════════════════════════════════════════════════
else
    echo ">> Packaging Linux binary…"
    cp "$BINARY" "$BUILD_DIR/$APP_NAME"
    (cd "$BUILD_DIR" && tar czf "$APP_NAME-$VERSION-linux.tar.gz" "$APP_NAME" > /dev/null)
    echo ">> Archive: $BUILD_DIR/$APP_NAME-$VERSION-linux.tar.gz"
fi

echo ""
echo "✔ Done. Packages in: $BUILD_DIR"
ls -lh "$BUILD_DIR/"
