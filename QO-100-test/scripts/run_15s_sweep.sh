#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

exec python3 QO-100-test/scripts/qo100_sweep.py --seconds 15 "$@"
