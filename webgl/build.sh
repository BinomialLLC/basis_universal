#!/usr/bin/env bash
#
# Build the WebAssembly transcoder and/or encoder with Emscripten.
# Runs `emcmake cmake ../ && make` in each project's build/ dir.
# Stops on the first error.
#
# Usage:
#   ./build.sh                      # build both (transcoder, then encoder)
#   ./build.sh transcoder           # build only the transcoder
#   ./build.sh encoder              # build only the encoder
#   ./build.sh --clean              # make clean + wipe CMake cache first (full rebuild)
#   ./build.sh --slow               # serial build (plain make, one file at a time)
#   ./build.sh encoder -DKTX2_ZSTANDARD=OFF -DCMAKE_BUILD_TYPE=Debug
#
# Any -D... argument is forwarded to cmake. Runnable from any directory.
#
set -euo pipefail

# Absolute path to this script's directory (webgl/), so CWD doesn't matter.
WEBGL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---- pretty output (color only on a terminal) ----
if [ -t 1 ]; then B=$'\e[1m'; G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; N=$'\e[0m'; else B=; G=; R=; Y=; N=; fi
info() { printf '%s==>%s %s\n' "$B$G" "$N" "$*"; }
die()  { printf '%serror:%s %s\n' "$B$R" "$N" "$*" >&2; exit 1; }

# ---- parse args ----
CLEAN=0
SLOW=0
TARGETS=()
CMAKE_ARGS=()
for a in "$@"; do
  case "$a" in
    -c|--clean)         CLEAN=1 ;;
    --slow)             SLOW=1 ;;
    transcoder|encoder) TARGETS+=("$a") ;;
    -D*)                CMAKE_ARGS+=("$a") ;;
    -h|--help)          sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *)                  die "unknown argument: '$a' (try --help)" ;;
  esac
done
# Default: build both. Transcoder first (quick), then encoder.
[ "${#TARGETS[@]}" -eq 0 ] && TARGETS=(transcoder encoder)

# ---- toolchain check (auto-source emsdk if present but not on PATH) ----
if ! command -v emcmake >/dev/null 2>&1 && [ -n "${EMSDK:-}" ] && [ -f "$EMSDK/emsdk_env.sh" ]; then
  info "emcmake not on PATH; sourcing \$EMSDK/emsdk_env.sh"
  # shellcheck disable=SC1091
  source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1 || true
fi
command -v emcmake >/dev/null 2>&1 || die "emcmake not found. Activate emsdk first, e.g.: source /path/to/emsdk/emsdk_env.sh"
command -v cmake   >/dev/null 2>&1 || die "cmake not found."

JOBS="$(nproc 2>/dev/null || echo 4)"
# --slow => serial (one file at a time, the classic `make`); otherwise all cores.
if [ "$SLOW" -eq 1 ]; then MAKEJ=(-j1); JOBDESC="serial (make -j1)"; else MAKEJ=(-j"$JOBS"); JOBDESC="make -j$JOBS"; fi

# ---- build one project ----
build_one() {
  local name="$1" dir="$WEBGL_DIR/$1/build"
  [ -f "$WEBGL_DIR/$1/CMakeLists.txt" ] || die "no CMakeLists.txt in webgl/$1"
  info "Building $name -> $1/build  ($JOBDESC)"
  mkdir -p "$dir"
  if [ "$CLEAN" -eq 1 ]; then
    # make clean first (removes build outputs + objects via the existing build system),
    # then wipe the CMake cache/state so the next configure starts fresh (and picks up any -D changes).
    if [ -f "$dir/Makefile" ]; then
      info "  make clean"
      ( cd "$dir" && make clean ) || info "  (make clean failed; wiping cache anyway)"
    fi
    info "  removing CMake cache + build state"
    rm -rf "$dir/CMakeCache.txt" "$dir/CMakeFiles"
  fi
  ( cd "$dir" && emcmake cmake ../ "${CMAKE_ARGS[@]}" && make "${MAKEJ[@]}" )
}

# ---- go ----
for t in "${TARGETS[@]}"; do
  build_one "$t"
done

# ---- summary ----
info "Success. Built artifacts:"
for t in "${TARGETS[@]}"; do
  find "$WEBGL_DIR/$t/build" -maxdepth 1 -name '*.wasm' -printf '    %p (%s bytes)\n' 2>/dev/null | sort
done
