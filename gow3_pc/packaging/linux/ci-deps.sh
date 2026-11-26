#!/usr/bin/env bash
# Linux build dependencies for the native Ubuntu 26.04 build.
# This is shared with the Last of Us Linux build workflow: keep the same generic amd64
# toolchain and Vulkan/SDL versions so the output remains runnable on Sandy Bridge.
set -euo pipefail

. /etc/os-release
if [[ ${ID:-} != ubuntu || ${VERSION_ID:-} != 26.04 ]]; then
    echo "This dependency script targets Ubuntu 26.04; detected ${PRETTY_NAME:-unknown}." >&2
    exit 1
fi

sudo_cmd=()
if [[ $(id -u) -ne 0 ]]; then sudo_cmd=(sudo); fi
export DEBIAN_FRONTEND=noninteractive

"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" apt-get install -y --no-install-recommends \
    build-essential ccache gcc-14 g++-14 cmake ninja-build pkg-config git curl ca-certificates file patchelf binutils \
    libvulkan-dev glslang-tools libboost-dev libfmt-dev libxxhash-dev libzydis-dev robin-map-dev \
    libspirv-cross-c-shared-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxfixes-dev \
    libxinerama-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev \
    libegl-dev libgles2-mesa-dev libgl-dev libasound2-dev libpulse-dev libpipewire-0.3-dev \
    libudev-dev libdbus-1-dev libibus-1.0-dev libsamplerate0-dev \
    python3 python3-venv python3-tk python3-dev

"${sudo_cmd[@]}" update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 140 --slave /usr/bin/g++ g++ /usr/bin/g++-14
"${sudo_cmd[@]}" update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 140
"${sudo_cmd[@]}" update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 140

echo "C compiler: $(gcc --version | head -1)"
echo "C++ compiler: $(g++ --version | head -1)"

# Match the successful Linux runtime build: Vulkan-Headers 1.4.357.0, VMA 3.1.0, SDL3 3.4.16.
VULKAN_HEADERS="${VULKAN_HEADERS:-vulkan-sdk-1.4.357.0}"
VMA="${VMA:-v3.1.0}"
SDL="${SDL:-3.4.16}"
cache_marker=/usr/local/share/uncharted-linux-build-deps.txt

if [[ "${GOW3_NATIVE_DEPS_CACHE_HIT:-false}" == "true" ]] &&
   [[ -f "$cache_marker" ]] &&
   grep -Fxq "Vulkan-Headers=$VULKAN_HEADERS" "$cache_marker" &&
   grep -Fxq "VMA=$VMA" "$cache_marker" &&
   grep -Fxq "SDL3=$SDL" "$cache_marker" &&
   [[ -f /usr/local/include/vulkan/vulkan.hpp ]] &&
   [[ -f /usr/local/include/vk_mem_alloc.h ]] &&
   [[ "$(pkg-config --modversion sdl3 2>/dev/null || true)" == "$SDL" ]]; then
    "${sudo_cmd[@]}" ldconfig
    echo "Using cached native dependencies: Vulkan-Headers $VULKAN_HEADERS, VMA $VMA, SDL3 $SDL"
    echo "Ccache: $(ccache --version | head -1)"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git clone -q --depth 1 --branch "$VULKAN_HEADERS" https://github.com/KhronosGroup/Vulkan-Headers "$work/vulkan-headers"
cmake -S "$work/vulkan-headers" -B "$work/vulkan-headers/build" -G Ninja \
    -DCMAKE_INSTALL_PREFIX=/usr/local >/dev/null
"${sudo_cmd[@]}" cmake --install "$work/vulkan-headers/build" >/dev/null

"${sudo_cmd[@]}" curl -fsSL -o /usr/local/include/vk_mem_alloc.h \
    "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/$VMA/include/vk_mem_alloc.h"
grep -q "VmaAllocationInfo2" /usr/local/include/vk_mem_alloc.h
grep -q "vmaGetAllocationInfo2" /usr/local/include/vk_mem_alloc.h

curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL3-$SDL.tar.gz" | tar xz -C "$work"
cmake -S "$work/SDL3-$SDL" -B "$work/sdl" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >/dev/null
ninja -C "$work/sdl" >/dev/null
"${sudo_cmd[@]}" ninja -C "$work/sdl" install >/dev/null
"${sudo_cmd[@]}" ldconfig

echo "Vulkan-Headers: $VULKAN_HEADERS"
echo "VMA: $VMA"
echo "SDL3: $(pkg-config --modversion sdl3)"
echo "FFmpeg: $(pkg-config --modversion libavcodec)"
echo "fmt: $(dpkg-query -W -f='${Version}' libfmt-dev)"
echo "Zydis: $(dpkg-query -W -f='${Version}' libzydis-dev)"
test -f /usr/local/include/vulkan/vulkan.hpp
test -f /usr/local/include/vk_mem_alloc.h
mkdir -p /usr/local/share
{
    printf 'Vulkan-Headers=%s\n' "$VULKAN_HEADERS"
    printf 'VMA=%s\n' "$VMA"
    printf 'SDL3=%s\n' "$SDL"
} > "$cache_marker"
