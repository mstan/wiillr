#!/usr/bin/env bash
# Same CLI as tools/build.py; no automatic launch.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$(uname -s)" == Linux ]] || { echo 'Run this wrapper on Linux.' >&2; exit 2; }
exec "${WIILLR_PYTHON:-python3}" "$ROOT/tools/build.py" "$@"
