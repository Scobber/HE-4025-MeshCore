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

run_dragino_feed_setup() {
    local log_file
    log_file="$(mktemp)"

    set +e
    (cd "$SDK_DIR" && ./set_up_build_environment.sh) >"$log_file" 2>&1
    local status=$?
    set -e

    awk '
        /^WARNING: No feed for source package '\''lua'\'' found$/ {
            ignored++
            next
        }
        /^WARNING: No feed for package '\''(libc|libssp|librt|libpthread)'\'' found$/ {
            ignored++
            next
        }
        { print }
        END {
            if (ignored > 0) {
                printf("Note: suppressed %d known Dragino/OpenWrt feed warnings for lua/libc/libssp/librt/libpthread.\n", ignored)
            }
        }
    ' "$log_file"
    rm -f "$log_file"

    return "$status"
}

patch_dragino_prereqs() {
    local prereq_file="$SDK_DIR/openwrt/include/prereq-build.mk"

    if [ ! -f "$prereq_file" ]; then
        echo "Expected OpenWrt prereq file not found: $prereq_file" >&2
        exit 1
    fi

    if grep -q "\\[1-9\\]\\[0-9\\]" "$prereq_file"; then
        return
    fi

    echo "Patching Dragino GCC prerequisite regex for modern host GCC versions"
    sed -i.bak \
        -e "s#grep -E '^(4\\\\\\.\\[8-9\\]|\\[5-9\\]\\\\\\.?)'#grep -E '^(4[.][8-9]|[5-9][.]?|[1-9][0-9][.]?)'#g" \
        "$prereq_file"
}

configure_dragino_download_mirrors() {
    local mirror_file="$SDK_DIR/openwrt/scripts/localmirrors"
    local download_script="$SDK_DIR/openwrt/scripts/download.pl"

    if [ ! -d "$SDK_DIR/openwrt/scripts" ]; then
        echo "Expected OpenWrt scripts directory not found: $SDK_DIR/openwrt/scripts" >&2
        exit 1
    fi
    if [ ! -f "$download_script" ]; then
        echo "Expected OpenWrt download script not found: $download_script" >&2
        exit 1
    fi

    echo "Configuring current OpenWrt source mirrors and removing Dragino's stale LEDE mirror"
    printf '%s\n' \
        "https://sources.openwrt.org" \
        "https://sources.cdn.openwrt.org" \
        "https://downloads.openwrt.org/sources" \
        > "$mirror_file"

    sed -i.bak \
        "/push @mirrors, 'https:\\/\\/sources\\.lede-project\\.org';/d" \
        "$download_script"
    rm -f "$download_script.bak"

    if grep -q "sources.lede-project.org" "$download_script"; then
        echo "Failed to remove stale sources.lede-project.org mirror from $download_script" >&2
        exit 1
    fi
}

install_dragino_host_tool_patches() {
    local patch_src="$REPO_ROOT/firmware/host-tool-patches/m4/100-fix-sigstksz.patch"
    local patch_src_glibc="$REPO_ROOT/firmware/host-tool-patches/m4/101-fix-ftbfs-with-glibc-2.28.patch"
    local patch_dir="$SDK_DIR/openwrt/tools/m4/patches"
    local patch_dst="$patch_dir/100-fix-sigstksz.patch"
    local patch_dst_glibc="$patch_dir/101-fix-ftbfs-with-glibc-2.28.patch"

    if [ ! -f "$patch_src" ]; then
        echo "Expected host m4 patch not found: $patch_src" >&2
        exit 1
    fi
    if [ ! -f "$patch_src_glibc" ]; then
        echo "Expected host m4 patch not found: $patch_src_glibc" >&2
        exit 1
    fi

    echo "Installing host m4 compatibility patches"
    mkdir -p "$patch_dir"
    cp "$patch_src" "$patch_dst"
    cp "$patch_src_glibc" "$patch_dst_glibc"
}

patch_dragino_qmi_wwan_q() {
    local pkg_makefile="$SDK_DIR/openwrt/package/kernel/qmi-wwan-q/Makefile"

    if [ ! -f "$pkg_makefile" ]; then
        return
    fi

    if grep -q 'DEPENDS:=+kmod-usb-wdm +kmod-usb-core +kmod-usb-net' "$pkg_makefile"; then
        return
    fi

    echo "Updating Dragino qmi-wwan-q dependencies to current OpenWrt package names"
    sed -i.bak \
        -e 's#DEPENDS:=+cdc-wdm +usbcore +usbnet#DEPENDS:=+kmod-usb-wdm +kmod-usb-core +kmod-usb-net#' \
        "$pkg_makefile"
    rm -f "$pkg_makefile.bak"
}

firmware_image_exists() {
    [ -d "$SDK_DIR/image" ] || return 1
    find "$SDK_DIR/image" -type f -name "*-v${VERSION}-squashfs-sysupgrade.bin" -print -quit | grep -q .
}

clear_previous_versioned_images() {
    [ -d "$SDK_DIR/image" ] || return 0
    find "$SDK_DIR/image" -type f -name "*-v${VERSION}-squashfs-sysupgrade.bin" -exec rm -f {} +
}

run_dragino_image_build() {
    local mode="$1"

    case "$mode" in
        single)
            (cd "$SDK_DIR" && ./build_image.sh -a "$APP" -v "$VERSION" -s)
            ;;
        parallel)
            (cd "$SDK_DIR" && ./build_image.sh -a "$APP" -v "$VERSION")
            ;;
        *)
            echo "Unknown build mode: $mode" >&2
            exit 1
            ;;
    esac
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
    run_dragino_feed_setup
fi

patch_dragino_prereqs
configure_dragino_download_mirrors
install_dragino_host_tool_patches
patch_dragino_qmi_wwan_q

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
mkdir -p "$FILES_DIR/etc/config"

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

download_jobs="${DOWNLOAD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
if ! [[ "$download_jobs" =~ ^[0-9]+$ ]] || [ "$download_jobs" -lt 1 ]; then
    download_jobs=2
fi

echo "Pre-downloading OpenWrt sources with $download_jobs jobs"
(cd "$SDK_DIR/openwrt" && make download -j"$download_jobs" V=s)

clear_previous_versioned_images

echo "Building firmware app=$APP version=$VERSION board_profile=$BOARD_PROFILE"
if [ "$SINGLE_THREAD" -eq 1 ]; then
    run_dragino_image_build single
else
    set +e
    run_dragino_image_build parallel
    build_status=$?
    set -e

    if [ "$build_status" -ne 0 ]; then
        echo "Parallel Dragino build exited with status $build_status; rerunning single-threaded for a useful failure log"
        run_dragino_image_build single
    elif ! firmware_image_exists; then
        echo "Parallel Dragino build did not produce a sysupgrade image; rerunning single-threaded for a useful failure log"
        run_dragino_image_build single
    fi
fi

if ! firmware_image_exists; then
    echo "No versioned sysupgrade image was produced for version $VERSION" >&2
    echo "Expected something like: $SDK_DIR/image/*/*-v$VERSION-squashfs-sysupgrade.bin" >&2
    exit 1
fi

echo "Firmware images are under: $SDK_DIR/image"
echo "Use the generated *-squashfs-sysupgrade.bin for OTA/sysupgrade."
