#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds Nebula for Linux into the program folder installer/package-linux.sh packs: the SDL host with the Vulkan
# renderer (nebula, libSDL3.so), the game module with and without the function rows (libsmg_game.so,
# libsmg_game_plain.so) and its self-description, and the game importer (nebula-import, libnebula_import.so).
#
#   platform/linux/build_linux.sh -d <deps> -l <llvm> -w <work folder> [-j <jobs>]
#
#   <deps>   the dependency checkouts platform/sdl/CMakeLists.txt names (SDL3, glslang-src, Vulkan-Headers, volk,
#            VulkanMemoryAllocator)
#   <llvm>   LLVM 21 or later, whose clang builds everything: the game module's row generator needs its libclang
#   -j       how many compiles run at once (default: the build tool's own choice)
#
# The program folder is <work>/bin. The game module is the players' build (ThinLTO, native/CMakeLists.txt NEBULA_LTO).
# cmake, ninja, python3 and cargo are taken from PATH.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
deps=
llvm=
work=
jobs=
while getopts "d:l:w:j:" option; do
    case "$option" in
    d) deps=$OPTARG ;;
    l) llvm=$OPTARG ;;
    w) work=$OPTARG ;;
    j) jobs="--parallel $OPTARG" ;;
    *) sed -n '7,12p' "$0" >&2; exit 2 ;;
    esac
done
if [ -z "$deps" ] || [ -z "$llvm" ] || [ -z "$work" ]; then
    sed -n '7,12p' "$0" >&2
    exit 2
fi
for tool in clang clang++; do
    [ -x "$llvm/bin/$tool" ] || { echo "build_linux.sh: no $tool in $llvm/bin" >&2; exit 2; }
done
mkdir -p "$work"
deps=$(cd "$deps" && pwd)
llvm=$(cd "$llvm" && pwd)
work=$(cd "$work" && pwd)

# Every platform's release items (permissions, usage texts, file and link associations) are in the tree.
python3 "$root/tools/check_platform_items.py" "$root"

compilers="-DCMAKE_C_COMPILER=$llvm/bin/clang -DCMAKE_CXX_COMPILER=$llvm/bin/clang++"

# The host: the SDL program, which makes its shader tools first (platform/sdl/CMakeLists.txt NEBULA_HOST_TOOLS).
# shellcheck disable=SC2086
cmake -S "$root/platform/sdl" -B "$work/host" -G Ninja -DCMAKE_BUILD_TYPE=Release $compilers "-DNEBULA_DEPS=$deps"
# shellcheck disable=SC2086
cmake --build "$work/host" --target main $jobs

# The game module: both builds of it, the self-description and the services list.
# shellcheck disable=SC2086
cmake -S "$root/native" -B "$work/game" -G Ninja -DCMAKE_BUILD_TYPE=Release $compilers -DNEBULA_LTO=ON
# shellcheck disable=SC2086
cmake --build "$work/game" --target smg_game smg_game_plain luma_self_description luma_services_file $jobs

# The importer: the program Setup's import runs and the library the game cooks mods' files with. The source paths it
# keeps (panic texts) name no build machine's folders.
cargo_home=${CARGO_HOME:-$HOME/.cargo}
rustup_home=${RUSTUP_HOME:-$HOME/.rustup}
(
    cd "$root"
    export CARGO_TARGET_DIR="$work/cargo"
    export RUSTFLAGS="--remap-path-prefix=$cargo_home=cargo --remap-path-prefix=$rustup_home=rustup --remap-path-prefix=$root=nebula"
    cargo build --profile dist --locked -p nebula-import --bin nebula-import --lib
)

# The program folder, as installer/package-linux.sh reads it.
bin="$work/bin"
rm -rf "$bin"
mkdir -p "$bin/luma"
cp "$work/host/nebula" "$bin/nebula"
sdl=$(find "$work/host" -name 'libSDL3.so.0' | head -n 1)
[ -n "$sdl" ] || { echo "build_linux.sh: the host build has no libSDL3.so.0" >&2; exit 1; }
cp -P "$(dirname "$sdl")"/libSDL3.so* "$bin/"
cp "$work/game/libsmg_game.so" "$work/game/libsmg_game_plain.so" "$bin/"
cp "$work/cargo/dist/nebula-import" "$work/cargo/dist/libnebula_import.so" "$bin/"
cp -R "$work/game/luma/." "$bin/luma/"
for file in luma_types/luma_types.json luma_types/luma_functions.json luma_types/luma_numbers.json \
    luma_types/luma_services.json smg_game.luma-placement.json smg_game.luma-self.json; do
    cp "$work/game/$file" "$bin/luma/"
done
echo "$bin"
