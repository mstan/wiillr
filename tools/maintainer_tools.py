# SPDX-License-Identifier: GPL-3.0-or-later
"""Load the immutable GC/Wii maintainer-tool provider pinned by this engine."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1] / "external/gcnllr-tools/tools"
if not (ROOT / "debug_transport.py").is_file() or not (ROOT / "artifact_closure.py").is_file():
    raise RuntimeError("Initialize the pinned external/gcnllr-tools submodule")
sys.path.insert(0, str(ROOT))
