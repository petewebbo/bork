#!/bin/sh
set -eu

case "$#:${1-}" in
    0:) use_jack=OFF ;;
    1:USE_JACK) use_jack=ON ;;
    *) echo "Usage: $0 [USE_JACK]" >&2; exit 2 ;;
esac

cmake \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DUSE_JACK="$use_jack" \
    -S . \
    -B .build

ln -sf .build/compile_commands.json ./compile_commands.json
cmake --build .build
