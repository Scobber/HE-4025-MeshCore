#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${BUILD_ROOT:-$REPO_ROOT/build}"
SDK_DIR="${SDK_DIR:-$BUILD_ROOT/linino}"
APP="${APP:-meshcore-he4025}"
DRAGINO_REPO="${DRAGINO_REPO:-https://github.com/dragino/linino.git}"

usage() {
    cat <<EOF
Usage: $0 [--sdk DIR] [--build-root DIR] [--app NAME]

Builds a full Dragino Linino/Yun firmware image for HE/MS14 style boards with
the meshcore-he4025 daemon, embedded stats web UI, and OTA endpoint included.

The Dragino Linino build system emits:
  trunk/image/APP-build--v1.3.5--DATE/dragino2-yun-APP-v1.3.5-squashfs-sysupgrade.bin

Environment overrides:
  SDK_DIR       Existing or target dragino/linino checkout
  BUILD_ROOT    Directory for cloning when SDK_DIR is not set
  APP           Dragino application name, default meshcore-he4025
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sdk)
            SDK_DIR="$2"
            shift 2
            ;;
        --build-root)
            BUILD_ROOT="$2"
            SDK_DIR="$BUILD_ROOT/linino"
            shift 2
            ;;
        --app)
            APP="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

mkdir -p "$BUILD_ROOT"

if [ ! -d "$SDK_DIR/.git" ]; then
    echo "Cloning Dragino Linino into $SDK_DIR"
    git clone "$DRAGINO_REPO" "$SDK_DIR"
fi

TRUNK_DIR="$SDK_DIR/trunk"
if [ ! -f "$TRUNK_DIR/build_image.sh" ]; then
    echo "Not a Dragino Linino checkout: missing $TRUNK_DIR/build_image.sh" >&2
    exit 1
fi

PKG_DIR="$TRUNK_DIR/package/meshcore-he4025"
FILES_DIR="$TRUNK_DIR/files-$APP"
CONFIG_FILE="$TRUNK_DIR/.config.$APP"

echo "Installing local OpenWrt package into $PKG_DIR"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"
cp -R "$REPO_ROOT/src" "$REPO_ROOT/tools" "$REPO_ROOT/boards" "$REPO_ROOT/openwrt" "$PKG_DIR/"
cp "$REPO_ROOT/daemon.mk" "$PKG_DIR/daemon.mk"
cp "$REPO_ROOT/openwrt/Makefile" "$PKG_DIR/Makefile"

echo "Installing firmware overlay into $FILES_DIR"
rm -rf "$FILES_DIR"
cp -R "$REPO_ROOT/firmware/files-meshcore-he4025" "$FILES_DIR"

if [ ! -f "$TRUNK_DIR/.config.common" ]; then
    echo "Expected Linino base config not found: $TRUNK_DIR/.config.common" >&2
    exit 1
fi

echo "Creating $CONFIG_FILE from Linino .config.common"
cp "$TRUNK_DIR/.config.common" "$CONFIG_FILE"
sed -i.bak \
    -e '/CONFIG_PACKAGE_meshcore-he4025/d' \
    -e '/CONFIG_PACKAGE_kmod-spi-dev/d' \
    -e '/CONFIG_PACKAGE_uclibcxx/d' \
    -e '/CONFIG_PACKAGE_libstdcpp/d' \
    -e '/CONFIG_INSTALL_LIBSTDCPP/d' \
    -e '/CONFIG_USE_UCLIBCXX/d' \
    -e '/CONFIG_USE_LIBSTDCXX/d' \
    "$CONFIG_FILE"
rm -f "$CONFIG_FILE.bak"
cat "$REPO_ROOT/firmware/meshcore-he4025.linino.config.append" >> "$CONFIG_FILE"

echo "Resolving Linino config dependencies"
cp "$CONFIG_FILE" "$TRUNK_DIR/.config"
(cd "$TRUNK_DIR" && make defconfig)
cp "$TRUNK_DIR/.config" "$CONFIG_FILE"

echo "Building Dragino Linino app=$APP"
(cd "$TRUNK_DIR" && ./build_image.sh "$APP")

echo "Firmware images are under: $TRUNK_DIR/image"
echo "Reminder: Linino's stock mach-linino board file does not register an SX1276 spidev device."
echo "Confirm /dev/spidevX.Y and the IBB-v1.0 radio wiring before expecting direct SPI to work."
