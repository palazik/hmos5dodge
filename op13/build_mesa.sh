#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Build Mesa for the OP13 HarmonyOS guest: freedreno (GLES) and Turnip (Vulkan)
# on the Qualcomm KGSL kernel driver, plus Zink and softpipe, with the OHOS EGL
# platform from hmos2sweet-reference/src/mesa/0001-mesa-ohos-platform-for-sweet.patch.
#
# Source: lfdevs/mesa-for-android-container dev/adreno-main 1e6ee4d (27.0.0-devel,
# 2026-11-18), the Adreno/KGSL fork the reference port used, but new enough for
# Adreno 830 (gen80000).  The 26.1 snapshot shipped before faulted the GPU on its
# first submit ("CP | Protected mode error | WRITE | addr=0x09313").
#
# Usage: op13/build_mesa.sh   (from hmos5dodge/, after the source is unpacked to
#        ../work/mesa-adreno and patched)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
W=$(cd "$HERE/../work" && pwd)
SRC=${MESA_SRC:-$W/mesa-adreno}
B=${MESA_BUILD:-$W/mesa-build}
O=$HERE/build/work/openharmony
export SYSROOT=$HERE/build/work/op13-sysroot
export PATH=$W/mesa-venv/bin:$PATH
OUT=${MESA_OUT:-$HERE/../out/op13-mesa}
mkdir -p "$B/pc" "$OUT"
# Pristine source: lfdevs/mesa-for-android-container @ 1e6ee4d (codeload tarball).
# op13/mesa/0001-op13-ohos-platform-mesa27.patch = reference OHOS EGL platform,
# ported to the Mesa 27 DRI loader API, plus EGL_PLATFORM_OHOS_KHR (0x34E0) and
# EGL_NATIVE_BUFFER_OHOS (0x34E1) as used by HarmonyOS libEGL/render_service,
# 'ohos' meson choices, and no KMS display WSI (musl here has no pthread_cancel).
if [ ! -f "$SRC/src/egl/drivers/dri2/platform_ohos.c" ]; then
  patch -d "$SRC" -p1 -N < "$HERE/op13/mesa/0001-op13-ohos-platform-mesa27.patch"
fi

pc() { printf 'Name: %s\nDescription: %s\nVersion: %s\nLibs: -L%s -l%s\nCflags: %s\n' "$1" "$1" "$2" "$SYSROOT/usr/lib" "$3" "$4" > "$B/pc/$1.pc"; }
pc libdrm 2.4.120 drm "-I$O/third_party_libdrm -I$O/third_party_libdrm/include/drm"
pc libhilog 1.0 hilog "-I$O/hiviewdfx_hilog/interfaces/native/innerkits/include"
pc libsurface 1.0 surface.z "-I$O/graphic_graphic_surface/interfaces/inner_api/surface -I$O/graphic_graphic_surface/interfaces/inner_api/utils -I$O/drivers_peripheral/display/interfaces/include -I$O/graphic_graphic_surface/interfaces/inner_api/common"
pc zlib 1.2.13 z "-I$SYSROOT/usr/include"

cat > "$B/cross.ini" <<EOC
[binaries]
c = '$HERE/build/ohos-clang'
cpp = '$HERE/build/ohos-clang++'
ar = 'llvm-ar'
strip = 'llvm-strip'
pkg-config = 'pkg-config'
[built-in options]
c_args = ['-O2', '-fno-emulated-tls', '-Wno-error', '-Wno-int-conversion', '-Wno-incompatible-function-pointer-types']
cpp_args = ['-O2', '-fno-emulated-tls', '-Wno-error']
c_link_args = ['-Wl,--no-undefined', '-Wl,--undefined-version', '$SYSROOT/usr/lib/libc.so']
cpp_link_args = ['-Wl,--no-undefined', '-Wl,--undefined-version', '$SYSROOT/usr/lib/libc++.so', '$SYSROOT/usr/lib/libc.so']
[properties]
needs_exe_wrapper = true
pkg_config_libdir = '$B/pc'
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'
EOC

OPTS=(--cross-file "$B/cross.ini" --buildtype=release
  -Dplatforms=ohos -Degl-native-platform=ohos -Dandroid-stub=false
  -Dgallium-drivers=freedreno,softpipe,zink -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl
  -Dgles1=enabled -Dgles2=enabled -Dopengl=false -Dgbm=disabled -Dglx=disabled -Degl=enabled
  -Dllvm=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dexpat=disabled
  -Dbuild-tests=false -Dtools=[] -Dvulkan-layers=[])
if [ -f "$B/b/build.ninja" ]; then meson setup --reconfigure "$B/b" "$SRC" "${OPTS[@]}"; else meson setup "$B/b" "$SRC" "${OPTS[@]}"; fi
[ "${CONFIGURE_ONLY:-0}" = 1 ] && exit 0
ninja -C "$B/b"

F=$OUT/lib64/chipsetsdk
mkdir -p "$F" "$OUT/etc/vulkan/icd.d"
llvm-strip -o "$F/libEGL_impl.so" "$(find "$B/b" -name 'libEGL.so*' -type f | head -1)"
llvm-strip -o "$F/libGLESv1_impl.so" "$(find "$B/b" -name 'libGLESv1_CM.so*' -type f | head -1)"
llvm-strip -o "$F/libGLESv2_impl.so" "$(find "$B/b" -name 'libGLESv2.so*' -type f | head -1)"
cp "$F/libGLESv2_impl.so" "$F/libGLESv3_impl.so"
llvm-strip -o "$F/$(basename "$(find "$B/b" -name 'libgallium-*.so' -type f | head -1)")" "$(find "$B/b" -name 'libgallium-*.so' -type f | head -1)"
llvm-strip -o "$F/libvulkan_freedreno.so" "$(find "$B/b" -name 'libvulkan_freedreno.so' -type f | head -1)"
ls -la "$F"
echo "Mesa -> $OUT"
