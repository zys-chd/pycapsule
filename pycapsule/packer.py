"""
Packer — creates the project ZIP with embedded _pycapsule_/ config + bootstrap.

Produces a ZIP byte string that gets appended to the launcher binary.
"""

import os
import struct
import zlib
import json
from pathlib import Path


HERE = Path(__file__).parent.parent  # repo root


def build_config_txt(spec):
    """Convert a .spec dict into flat key=value config.txt format."""
    lines = []

    def add(key, value):
        if value is None:
            value = ""
        elif isinstance(value, bool):
            value = "1" if value else "0"
        lines.append(f"{key}={value}")

    add("app_name", spec.get("app_name", ""))
    add("app_version", spec.get("app_version", "1.0.0"))

    # python_min → major / minor
    py_min = spec.get("python_min", "3.10")
    parts = py_min.split(".")
    add("python_min_major", parts[0] if len(parts) > 0 else "3")
    add("python_min_minor", parts[1] if len(parts) > 1 else "10")

    add("python_url", spec.get("python_url", "https://www.python.org/downloads/"))
    add("zip_prefix", spec.get("app_name", ""))  # default to app_name
    add("entry", spec.get("entry", "main.py"))
    add("console", spec.get("console", False))
    add("stdout_file", spec.get("stdout_file", ""))
    add("stderr_file", spec.get("stderr_file", ""))

    # requirements
    reqs = spec.get("requirements", {})
    # also add hidden_imports as requirements
    hidden = spec.get("hidden_imports", [])
    for h in hidden:
        if h not in reqs:
            reqs[h] = ""

    add("requirements_count", str(len(reqs)))
    for i, (pkg, ver) in enumerate(reqs.items()):
        pip_spec = f"{pkg}{ver}" if ver else pkg
        add(f"req_{i}_import", pkg)
        add(f"req_{i}_pip", pip_spec)

    return "\n".join(lines) + "\n"


def create_zip(project_root, spec, bootstrap_path=None):
    """Create a ZIP byte string containing the project files plus
    _pycapsule_/config.txt and _pycapsule_/bootstrap.py.

    Args:
        project_root: Path to the user's project directory
        spec: .spec dict with packaging config
        bootstrap_path: Path to bootstrap.py (default: repo src/bootstrap.py)
    """
    root = Path(project_root).resolve()
    app_name = spec.get("app_name", root.name)
    zip_prefix = app_name

    # Locate bootstrap.py
    if bootstrap_path is None:
        bootstrap_path = HERE / "src" / "bootstrap.py"
    else:
        bootstrap_path = Path(bootstrap_path)

    if not bootstrap_path.exists():
        raise FileNotFoundError(f"bootstrap.py not found: {bootstrap_path}")

    # Exclude lists
    exclude_dirs = set(spec.get("exclude_dirs", []))
    exclude_dirs.update({"pycapsule", "__pycache__", ".git"})

    exclude_patterns = spec.get("exclude_files", [])

    # Collect files
    files = []
    _collect_files(root, root, exclude_dirs, exclude_patterns, files)

    # Generate config
    full_spec = dict(spec)
    full_spec["zip_prefix"] = zip_prefix
    config_txt = build_config_txt(full_spec)

    # Build ZIP entries
    entries = []
    local_headers = []
    offset = 0

    # 1. Add _pycapsule_/config.txt
    _add_zip_entry(
        entries, local_headers, offset,
        f"_pycapsule_/config.txt",
        config_txt.encode("utf-8"),
    )
    offset = sum(len(lh) for lh in local_headers)

    # 2. Add _pycapsule_/bootstrap.py
    bootstrap_data = bootstrap_path.read_bytes()
    _add_zip_entry(
        entries, local_headers, offset,
        f"_pycapsule_/bootstrap.py",
        bootstrap_data,
    )
    offset = sum(len(lh) for lh in local_headers)

    # 3. Add user project files
    for full_path, rel_path in files:
        data = Path(full_path).read_bytes()
        zip_name = f"{zip_prefix}/{rel_path.replace(os.sep, '/')}"
        _add_zip_entry(entries, local_headers, offset, zip_name, data)
        offset = sum(len(lh) for lh in local_headers)

    # Assemble ZIP
    result = bytearray()
    for lh in local_headers:
        result += lh
    cd_start = len(result)
    for _, cd in entries:
        result += cd
    cd_end = len(result)

    # EOCD
    result += struct.pack("<I", 0x06054b50)
    result += struct.pack("<H", 0) * 2
    result += struct.pack("<H", len(entries))
    result += struct.pack("<H", len(entries))
    result += struct.pack("<I", cd_end - cd_start)
    result += struct.pack("<I", cd_start)
    result += struct.pack("<H", 0)

    print(f"  Packed {len(files)} files → {len(result) / 1024:.1f} KB ZIP")
    return bytes(result)


def _collect_files(root, current, exclude_dirs, exclude_patterns, result):
    """Recursively collect files, respecting exclusions."""
    for entry in sorted(current.iterdir()):
        if entry.name.startswith(".") and entry.name != ".gitignore":
            continue
        if entry.is_dir():
            if entry.name in exclude_dirs:
                continue
            _collect_files(root, entry, exclude_dirs, exclude_patterns, result)
        else:
            if _matches_exclude(entry.name, exclude_patterns):
                continue
            rel = entry.relative_to(root)
            result.append((str(entry), str(rel)))


def _matches_exclude(filename, patterns):
    import fnmatch
    for pat in patterns:
        if fnmatch.fnmatch(filename, pat):
            return True
    return False


def _add_zip_entry(entries, local_headers, offset, zip_name, data,
                   compress_level=9):
    """Add a file to the in-memory ZIP structure."""
    crc = zlib.crc32(data) & 0xFFFFFFFF

    if compress_level > 0:
        compressed = zlib.compress(data, compress_level)
        if len(compressed) < len(data):
            method = 8
            comp_data = compressed[2:-4]
        else:
            method = 0
            comp_data = data
    else:
        method = 0
        comp_data = data

    comp_size = len(comp_data)
    uncomp_size = len(data)
    name_bytes = zip_name.encode("utf-8")

    # Local header
    local_header = bytearray()
    local_header += struct.pack("<I", 0x04034b50)
    local_header += struct.pack("<H", 20)
    local_header += struct.pack("<H", 0x0800)
    local_header += struct.pack("<H", method)
    local_header += struct.pack("<H", 0) * 2
    local_header += struct.pack("<I", crc)
    local_header += struct.pack("<I", comp_size)
    local_header += struct.pack("<I", uncomp_size)
    local_header += struct.pack("<H", len(name_bytes))
    local_header += struct.pack("<H", 0)
    local_header += name_bytes

    # Central directory entry
    cd_entry = bytearray()
    cd_entry += struct.pack("<I", 0x02014b50)
    cd_entry += struct.pack("<H", 20) * 2
    cd_entry += struct.pack("<H", 0x0800)
    cd_entry += struct.pack("<H", method)
    cd_entry += struct.pack("<H", 0) * 2
    cd_entry += struct.pack("<I", crc)
    cd_entry += struct.pack("<I", comp_size)
    cd_entry += struct.pack("<I", uncomp_size)
    cd_entry += struct.pack("<H", len(name_bytes))
    cd_entry += struct.pack("<H", 0) * 3  # extra, comment, disk
    cd_entry += struct.pack("<H", 0)       # internal attrs
    cd_entry += struct.pack("<I", 0)       # external attrs
    cd_entry += struct.pack("<I", offset)
    cd_entry += name_bytes

    entries.append((len(local_headers), cd_entry))
    local_headers.append(bytes(local_header) + comp_data)
