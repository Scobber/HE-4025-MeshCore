#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${BUILD_ROOT:-$REPO_ROOT/build}"
SDK_DIR="${SDK_DIR:-$BUILD_ROOT/openwrt_lede-18.06}"
APP="${APP:-meshcore-he4025}"
VERSION="${VERSION:-$("$REPO_ROOT/scripts/firmware-version.sh")}"
DRAGINO_REPO="${DRAGINO_REPO:-https://github.com/dragino/openwrt_lede-18.06.git}"
BOARD_PROFILE="${BOARD_PROFILE:-dragino-ibb-v1.0}"

usage() {
    cat <<EOF
Usage: $0 [--sdk DIR] [--build-root DIR] [--version VERSION] [--board-profile PROFILE] [--single-thread]

Builds a full Dragino HE/DRAGINO2 OpenWrt LEDE 18.06 sysupgrade image with
the meshcore-he4025 package, embedded stats web UI, and OTA endpoint included.

Default firmware source:
  https://github.com/dragino/openwrt_lede-18.06

Environment overrides:
  SDK_DIR       Existing or target Dragino SDK checkout
  BUILD_ROOT    Directory for cloning when SDK_DIR is not set
  VERSION       Firmware version suffix, auto-generated when omitted
  APP           Dragino app name, default meshcore-he4025
  BOARD_PROFILE Board config from boards/ without .conf, default dragino-ibb-v1.0
EOF
}

SINGLE_THREAD=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --sdk)
            SDK_DIR="$2"
            shift 2
            ;;
        --build-root)
            BUILD_ROOT="$2"
            SDK_DIR="$BUILD_ROOT/openwrt_lede-18.06"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --board-profile)
            BOARD_PROFILE="$2"
            shift 2
            ;;
        --single-thread)
            SINGLE_THREAD=1
            shift
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
    echo "Cloning Dragino LEDE SDK into $SDK_DIR"
    git clone "$DRAGINO_REPO" "$SDK_DIR"
fi

if [ ! -d "$SDK_DIR/openwrt/feeds" ]; then
    echo "Setting up Dragino feeds"
    (cd "$SDK_DIR" && ./set_up_build_environment.sh)
fi

PKG_DIR="$SDK_DIR/openwrt/package/meshcore-he4025"
FILES_DIR="$SDK_DIR/files-$APP"
CONFIG_FILE="$SDK_DIR/.config.$APP"

echo "Installing local OpenWrt package into $PKG_DIR"
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR"
cp -R "$REPO_ROOT/src" "$REPO_ROOT/tools" "$REPO_ROOT/boards" "$REPO_ROOT/openwrt" "$PKG_DIR/"
cp "$REPO_ROOT/daemon.mk" "$PKG_DIR/daemon.mk"
cp "$REPO_ROOT/openwrt/Makefile" "$PKG_DIR/Makefile"

echo "Installing firmware overlay into $FILES_DIR"
rm -rf "$FILES_DIR"
cp -R "$REPO_ROOT/firmware/files-meshcore-he4025" "$FILES_DIR"

BOARD_CONFIG="$REPO_ROOT/boards/$BOARD_PROFILE.conf"
if [ ! -f "$BOARD_CONFIG" ]; then
    echo "Unknown board profile: $BOARD_PROFILE" >&2
    echo "Expected config file: $BOARD_CONFIG" >&2
    exit 1
fi
echo "Installing board profile $BOARD_PROFILE as /etc/config/meshcore"
cp "$BOARD_CONFIG" "$FILES_DIR/etc/config/meshcore"

if [ ! -f "$SDK_DIR/.config.lgw" ]; then
    echo "Expected Dragino base config not found: $SDK_DIR/.config.lgw" >&2
    exit 1
fi

echo "Creating $CONFIG_FILE from Dragino .config.lgw"
cp "$SDK_DIR/.config.lgw" "$CONFIG_FILE"
sed -i.bak \
    -e '/CONFIG_PACKAGE_meshcore-he4025/d' \
    -e '/CONFIG_PACKAGE_kmod-spi-dev/d' \
    -e '/CONFIG_PACKAGE_uclibcxx/d' \
    -e '/CONFIG_PACKAGE_libstdcpp/d' \
    "$CONFIG_FILE"
rm -f "$CONFIG_FILE.bak"
cat "$REPO_ROOT/firmware/meshcore-he4025.config.append" >> "$CONFIG_FILE"

echo "Resolving OpenWrt config dependencies"
cp "$CONFIG_FILE" "$SDK_DIR/openwrt/.config"
(cd "$SDK_DIR/openwrt" && make defconfig)
cp "$SDK_DIR/openwrt/.config" "$CONFIG_FILE"

echo "Building firmware app=$APP version=$VERSION board_profile=$BOARD_PROFILE"
if [ "$SINGLE_THREAD" -eq 1 ]; then
    (cd "$SDK_DIR" && ./build_image.sh -a "$APP" -v "$VERSION" -s)
else
    (cd "$SDK_DIR" && ./build_image.sh -a "$APP" -v "$VERSION")
fi

echo "Firmware images are under: $SDK_DIR/image"
echo "Use the generated *-squashfs-sysupgrade.bin for OTA/sysupgrade."
