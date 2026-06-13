#!/usr/bin/env python3
"""
Pycapsule universal bootstrap — embedded in every packaged app.

Reads _pycapsule_/config.txt, sets up the Python environment,
redirects stdout/stderr if configured, and launches the user's entry point.

Args (passed through from C launcher):
    Any arguments after the script path are forwarded to the user's entry point
    as sys.argv. The C launcher passes all original CLI args along.
"""

import sys
import os


SELF_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SELF_DIR)


def load_config():
    """Read flat key=value config.txt (same format C launcher parses)."""
    cfg = {}
    config_path = os.path.join(SELF_DIR, "config.txt")
    if not os.path.exists(config_path):
        return cfg

    real_file = None
    # config.txt might be a symlink after ZIP extraction — resolve it
    try:
        real_file = open(config_path, "r", encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        try:
            real_file = open(config_path, "r", encoding="gbk")
        except OSError:
            return cfg

    with real_file:
        for line in real_file:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, _, value = line.partition("=")
                cfg[key.strip()] = value.strip()
    return cfg


def main():
    cfg = load_config()

    # ── stdout/stderr redirect ──
    stdout_file = cfg.get("stdout_file", "")
    stderr_file = cfg.get("stderr_file", "")

    if stdout_file:
        try:
            sys.stdout = open(stdout_file, "w", encoding="utf-8")
        except OSError:
            pass

    if stderr_file:
        try:
            if stderr_file == stdout_file:
                sys.stderr = sys.stdout
            else:
                sys.stderr = open(stderr_file, "w", encoding="utf-8")
        except OSError:
            pass

    # ── sys.path setup ──
    zip_prefix = cfg.get("zip_prefix", "")
    app_dir = os.path.join(ROOT_DIR, zip_prefix) if zip_prefix else ROOT_DIR
    sys.path.insert(0, app_dir)

    # ── Launch user entry point ──
    entry = cfg.get("entry", "main.py")
    entry_path = os.path.join(app_dir, entry)

    if not os.path.exists(entry_path):
        print(f"Error: entry point not found: {entry_path}", file=sys.stderr)
        sys.exit(1)

    # Prepare sys.argv: bootstrap.py arg0 → entry script, then user args
    # C launcher passes: pythonw.exe bootstrap.py [user_args...]
    # We want the user's app to see: [entry_path, user_args...]
    user_argv = [entry_path] + sys.argv[1:]

    with open(entry_path, "r", encoding="utf-8") as f:
        source = f.read()

    code = compile(source, entry_path, "exec")

    # Set up globals for the user's __main__
    exec_globals = {
        "__name__": "__main__",
        "__file__": entry_path,
        "__builtins__": __builtins__,
    }

    # Save/restore sys.argv
    old_argv = sys.argv
    sys.argv = user_argv
    try:
        exec(code, exec_globals)
    except SystemExit as e:
        sys.exit(e.code)
    except Exception:
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        sys.argv = old_argv


if __name__ == "__main__":
    main()
