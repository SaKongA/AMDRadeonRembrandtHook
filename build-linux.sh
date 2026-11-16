#!/bin/bash
set -euo pipefail

projectDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$projectDir"

case "${1:-all}" in
    all|kext)
        targets=(all)
        ;;
    -h|--help)
        printf 'Usage: bash build-linux.sh [all|kext]\n'
        exit 0
        ;;
    *)
        printf 'Unknown target: %s\n' "$1" >&2
        printf 'Usage: bash build-linux.sh [all|kext]\n' >&2
        exit 2
        ;;
esac

if [ "$#" -gt 1 ]; then
    printf 'Expected at most one target\n' >&2
    exit 2
fi

clang=${CLANG:-clang-21}
clangxx=${CLANGXX:-clang++-21}
linker=${CCTOOLS_LD:-"$projectDir/upstream/cctools/bin/x86_64-apple-darwin19-ld"}

for tool in make "$clang" "$clangxx" "$linker"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required tool not found: %s\n' "$tool" >&2
        exit 1
    fi
done

printf 'Building: %s\n' "${targets[*]}"
make -B "${targets[@]}" CLANG="$clang" CLANGXX="$clangxx" LD="$linker"
printf 'Build completed: %s\n' "${BUILD_DIR:-$projectDir/build}"
