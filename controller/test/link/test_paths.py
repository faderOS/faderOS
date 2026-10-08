"""Locate locally built probes for both Make and out-of-tree CMake builds."""
import os
from pathlib import Path
BUILD = Path(os.environ.get("PICOHOST_TEST_BUILD_DIR", Path(__file__).resolve().parents[2] / "build"))
def probe(name):
    return str(BUILD / name)
