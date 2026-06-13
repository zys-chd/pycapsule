"""
Pycapsule example — user-facing bootstrap.py template.

Copy this to your project root and customize it.
This is a SIMPLE bootstrap for projects that manage their own dependencies.
For auto-detection and full packaging, use `python -m pycapsule` instead.

The C launcher passes all CLI arguments through to this script as sys.argv.
"""
import sys
import os

# Add your project to sys.path
APP_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, APP_DIR)

# Import and run your main
# Replace 'main' with your actual entry function
if __name__ == "__main__":
    from main import main
    main()
