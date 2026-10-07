#!/usr/bin/env bash
# ==============================================================================
# Run build command inside the Sega Tower of Power development container
# ==============================================================================
set -euo pipefail

IMAGE_NAME="${TOWER_IMAGE:-sega-tower-dev:latest}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Build container if it doesn't already exist
if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
    echo ">> Building Docker image $IMAGE_NAME..."
    docker build -t "$IMAGE_NAME" -f "$PROJECT_ROOT/Dockerfile" "$PROJECT_ROOT"
fi

CMD="${*:-make}"

echo ">> Running inside $IMAGE_NAME: $CMD"
docker run --rm \
    -u "$(id -u):$(id -g)" \
    -v "$PROJECT_ROOT":/work \
    -w /work \
    "$IMAGE_NAME" \
    "$CMD"
