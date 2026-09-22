#!/bin/sh
# Builds the launcher for the device. Run from the repo root.
set -e
IMAGE=portarelauncher-build
container images inspect "$IMAGE" >/dev/null 2>&1 || \
    container build -t "$IMAGE" -f tools/Containerfile .
exec container run --rm --arch arm64 \
    --volume "$(pwd):/src" "$IMAGE" make "$@"
