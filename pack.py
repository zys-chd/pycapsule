#!/usr/bin/env python3
"""
Pycapsule — legacy convenience wrapper.

For new projects, use `python -m pycapsule` directly.
This script exists for backward compatibility with Pycapsule 1.0 projects.

Usage:
    python pack.py              # same as: python -m pycapsule
    python pack.py --gen-spec   # same as: python -m pycapsule --gen-spec
"""

import sys
from pathlib import Path

# Ensure pycapsule package is importable
sys.path.insert(0, str(Path(__file__).parent))

from pycapsule.cli import main
sys.exit(main())
