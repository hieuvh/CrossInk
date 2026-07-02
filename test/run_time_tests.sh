#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$DIR/run_time_format_test.sh"
bash "$DIR/run_time_persistence_test.sh"
echo "All time tests passed."
