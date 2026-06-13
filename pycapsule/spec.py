"""
.spec JSON file handling.

Default .spec template and loading/merging logic.
A .spec file defines what goes into the packaged app.
"""

import json
from pathlib import Path


DEFAULT_SPEC = {
    "app_name": "",
    "app_version": "1.0.0",
    "entry": "main.py",
    "icon": None,
    "console": False,
    "python_min": "3.10",
    "python_url": "https://www.python.org/downloads/",
    "stdout_file": None,
    "stderr_file": None,
    "exclude_dirs": ["__pycache__", ".git", ".venv", ".env", "build", "dist"],
    "exclude_files": ["*.pyc", "*.spec", "*.pyo"],
    "hidden_imports": [],
    "requirements": {},
    "data_dirs": [],
}


def load_spec(path):
    """Load a .spec JSON file and merge with defaults."""
    spec = dict(DEFAULT_SPEC)
    spec_path = Path(path)
    if spec_path.exists():
        with open(spec_path, "r", encoding="utf-8") as f:
            user_spec = json.load(f)
        spec.update(user_spec)
    return spec


def generate_spec(root_dir, output_path=None):
    """Auto-generate a .spec file by scanning the project.

    Returns the spec dict. If output_path is given, writes JSON to that path.
    """
    from .analyzer import scan_imports

    root = Path(root_dir).resolve()
    spec = dict(DEFAULT_SPEC)
    spec["app_name"] = root.name

    # Guess entry point
    for candidate in ["main.py", "app.py", "run.py", "__main__.py"]:
        if (root / candidate).exists():
            spec["entry"] = candidate
            break

    # Scan imports
    imports = scan_imports(root)
    for imp in imports:
        # Try to get installed version
        version = _get_installed_version(imp)
        spec["requirements"][imp] = version if version else ""

    if output_path:
        output = Path(output_path)
        with open(output, "w", encoding="utf-8") as f:
            json.dump(spec, f, indent=2, ensure_ascii=False)
        print(f"Generated: {output}")

    return spec


def _get_installed_version(package_name):
    """Try to get the installed version of a package via importlib."""
    try:
        import importlib.metadata
        return importlib.metadata.version(package_name)
    except Exception:
        return ""
