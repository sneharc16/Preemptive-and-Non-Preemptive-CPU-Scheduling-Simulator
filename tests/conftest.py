import os
import sys

import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


@pytest.fixture(scope="session")
def sched_bin():
    path = os.environ.get("SCHED_BIN", os.path.join(ROOT, "build", "sched"))
    if not os.path.isfile(path):
        pytest.exit(f"simulator binary not found at {path}; build first or set SCHED_BIN", 2)
    return os.path.abspath(path)
