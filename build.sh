#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
config=Release
build_dir="$root/build-linux"
clean=0
defines=()

for arg in "$@"; do
    case "$arg" in
        Debug|Release|RelWithDebInfo|MinSizeRel) config="$arg" ;;
        clean) clean=1 ;;
        -D*) defines+=("$arg") ;;
        *)
            echo "Usage: build.sh [Debug|Release|RelWithDebInfo|MinSizeRel] [clean] [-DVAR=VALUE ...]" >&2
            exit 1
            ;;
    esac
done

generator=()
if command -v ninja >/dev/null 2>&1; then generator=(-G Ninja); fi

if [ "$clean" = 1 ] && [ -d "$build_dir" ]; then
    echo "Cleaning $build_dir"
    rm -rf "$build_dir"
fi

cmake -S "$root" -B "$build_dir" ${generator[@]+"${generator[@]}"} \
    -DCMAKE_BUILD_TYPE="$config" ${defines[@]+"${defines[@]}"}
cmake --build "$build_dir" --parallel

echo
echo "Built $config: $build_dir/gittools ($(stat -c %s "$build_dir/gittools") bytes)"
