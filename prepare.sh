#!/bin/bash
set -euo pipefail

projectDir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
upstreamDir=${UPSTREAM_DIR:-"$projectDir/upstream"}

if [ "$#" -ne 0 ]; then
    printf 'Usage: bash prepare.sh\n' >&2
    exit 2
fi

if ! command -v git >/dev/null 2>&1; then
    printf 'Required tool not found: git\n' >&2
    exit 1
fi

mkdir -p "$upstreamDir"

prepareRepository() {
    local name=$1 url=$2 revision=$3
    local directory="$upstreamDir/$name" current remote

    if [ ! -e "$directory" ]; then
        git init -q "$directory"
        git -C "$directory" remote add origin "$url"
    elif [ ! -d "$directory/.git" ] && [ ! -f "$directory/.git" ]; then
        printf 'Not a Git repository: %s\n' "$directory" >&2
        return 1
    fi

    remote=$(git -C "$directory" remote get-url origin)
    if [ "${remote%.git}" != "${url%.git}" ]; then
        printf 'Unexpected origin for %s: %s\n' "$name" "$remote" >&2
        return 1
    fi

    current=$(git -C "$directory" rev-parse --verify HEAD 2>/dev/null || true)
    if [ "$current" = "$revision" ]; then
        printf '%s already at %s\n' "$name" "$revision"
        return 0
    fi
    if [ -n "$current" ]; then
        printf '%s is at %s, expected %s; existing checkout left unchanged.\n' "$name" "$current" "$revision" >&2
        return 1
    fi

    printf 'Fetching %s...\n' "$name"
    git -C "$directory" fetch --depth 1 origin "$revision"
    git -C "$directory" checkout --detach FETCH_HEAD
}

prepareRepository Lilu https://github.com/acidanthera/Lilu.git e4748cc081bf060302c7d3c44a643ce1d11b7e1d
prepareRepository MacKernelSDK https://github.com/acidanthera/MacKernelSDK.git 3f750085caa17ec3a7880f11c11bf4f48cd6a164
prepareRepository cctools-port https://github.com/tpoechtrager/cctools-port.git e79d784d667816e4b15a0abd78828f9abb0a0b99

printf 'Upstream sources ready: %s\n' "$upstreamDir"
