"""
AST-based import analyzer.

Scans all .py files in a project directory, finds explicit `import X`
and `from X import Y` statements, filters out stdlib, returns sorted list
of top-level third-party package names.
"""

import ast
import sys
from pathlib import Path


# Python 3.10+ has sys.stdlib_module_names
def _stdlib_names():
    if hasattr(sys, "stdlib_module_names"):
        return sys.stdlib_module_names
    # Fallback for older Python (shouldn't happen since we require 3.10)
    return frozenset({
        "abc", "aifc", "argparse", "array", "ast", "asyncio", "base64",
        "binascii", "bisect", "builtins", "bz2", "calendar", "cmath",
        "cmd", "code", "codecs", "codeop", "collections", "colorsys",
        "compileall", "concurrent", "configparser", "contextlib", "contextvars",
        "copy", "copyreg", "csv", "ctypes", "curses", "dataclasses",
        "datetime", "dbm", "decimal", "difflib", "dis", "distutils",
        "doctest", "email", "encodings", "enum", "errno", "faulthandler",
        "fcntl", "filecmp", "fileinput", "fnmatch", "fractions", "ftplib",
        "functools", "gc", "getopt", "getpass", "gettext", "glob", "grp",
        "gzip", "hashlib", "heapq", "hmac", "html", "http", "idlelib",
        "imaplib", "imghdr", "imp", "importlib", "inspect", "io",
        "ipaddress", "itertools", "json", "keyword", "lib2to3", "linecache",
        "locale", "logging", "lzma", "mailbox", "mailcap", "marshal",
        "math", "mimetypes", "mmap", "modulefinder", "multiprocessing",
        "netrc", "nis", "nntplib", "numbers", "operator", "os", "ossaudiodev",
        "pathlib", "pdb", "pickle", "pickletools", "pipes", "pkgutil",
        "platform", "plistlib", "poplib", "posix", "posixpath", "pprint",
        "profile", "pstats", "pty", "pwd", "py_compile", "pyclbr", "pydoc",
        "queue", "quopri", "random", "re", "readline", "reprlib",
        "resource", "rlcompleter", "runpy", "sched", "secrets", "select",
        "selectors", "shelve", "shlex", "shutil", "signal", "site",
        "smtpd", "smtplib", "sndhdr", "socket", "socketserver", "sqlite3",
        "ssl", "stat", "statistics", "string", "stringprep", "struct",
        "subprocess", "sunau", "symtable", "sys", "sysconfig", "syslog",
        "tabnanny", "tarfile", "telnetlib", "tempfile", "termios",
        "test", "textwrap", "threading", "time", "timeit", "tkinter",
        "token", "tokenize", "trace", "traceback", "tracemalloc", "tty",
        "turtle", "turtledemo", "types", "typing", "unicodedata",
        "unittest", "urllib", "uu", "uuid", "venv", "warnings", "wave",
        "weakref", "webbrowser", "winreg", "winsound", "wsgiref",
        "xdrlib", "xml", "xmlrpc", "zipapp", "zipfile", "zipimport", "zlib",
        "zoneinfo",
    })


_STDLIB = _stdlib_names()


def scan_imports(root_dir):
    """Scan all .py files in root_dir, return sorted list of non-stdlib
    top-level package names found via explicit import statements."""
    root = Path(root_dir).resolve()
    imports = set()

    for py_file in root.rglob("*.py"):
        # Skip hidden dirs, __pycache__, venvs
        parts = py_file.relative_to(root).parts
        if any(p.startswith(".") or p in ("__pycache__",) for p in parts):
            continue
        if any(p in (".venv", "venv", ".env", "build", "dist", "node_modules")
               for p in parts):
            continue

        try:
            source = py_file.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue

        try:
            tree = ast.parse(source, filename=str(py_file))
        except SyntaxError:
            continue

        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    top = alias.name.split(".")[0]
                    if top not in _STDLIB:
                        imports.add(top)

            elif isinstance(node, ast.ImportFrom):
                if node.module:
                    top = node.module.split(".")[0]
                    if top not in _STDLIB:
                        imports.add(top)

    return sorted(imports)
