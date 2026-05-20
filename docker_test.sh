#!/usr/bin/env bash
# docker_test.sh — Build, run, and verify the LogForge 3-node Docker cluster.
# Usage: Run from the project root.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

cleanup() {
    echo ""
    echo "==> Shutting down cluster..."
    docker compose down 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# =========================================================================== #
# 1. Build
# =========================================================================== #
echo "==> [1] Building Docker image..."
docker compose build
echo "    Done."

# =========================================================================== #
# 2. Start server cluster
# =========================================================================== #
echo ""
echo "==> [2] Starting follower1, follower2, leader..."
docker compose up -d follower1 follower2 leader

echo ""
echo "    Waiting 3s for cluster to initialize..."
sleep 3

# =========================================================================== #
# 3. Run client demo
# =========================================================================== #
echo ""
echo "==> [3] Running client demo..."
docker compose run --rm client

# =========================================================================== #
# 4. Print node logs
# =========================================================================== #
for node in leader follower1 follower2; do
    echo ""
    echo "################################################################"
    echo "#  $node logs"
    echo "################################################################"
    docker compose logs "$node"
done

# =========================================================================== #
# 5. Cleanup (handled by trap)
# =========================================================================== #
