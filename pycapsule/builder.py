"""
Builder - stitches pre-compiled launcher binary with the project ZIP.

Binary layout: [launcher.exe] [MAGIC "PCZP" (4B)] [zip_size LE (4B)] [ZIP data]
The C launcher reads the footer at runtime to find the embedded ZIP.
"""

import os
import sys
import struct
import shutil
import subprocess
from pathlib import Path


HERE = Path(__file__).parent
LAUNCHERS_DIR = HERE / "launchers"
SRC_DIR = HERE.parent / "src"

MAGIC = b"PCZP"


def get_launcher_path(console=False, target=None):
    """Find the pre-compiled launcher binary for the given platform."""
    if target is None:
        if sys.platform == "win32":
            target = "windows"
        elif sys.platform == "darwin":
            target = "macos"
        else:
            # Linux / WSL: default to cross-compiling for Windows
            target = os.environ.get("PYCAPSULE_TARGET", "windows")

    if target == "windows":
        suffix = "console" if console else "gui"
        name = f"launcher_win_x64_{suffix}.exe"
    elif target == "macos":
        name = "launcher_macos_x64"
    elif target == "linux":
        name = "launcher_linux_x64"
    else:
        raise ValueError(f"Unsupported target: {target}")

    path = LAUNCHERS_DIR / name
    if not path.exists():
        raise FileNotFoundError(
            f"Pre-compiled launcher not found: {path}\n"
            f"Run 'python -m pycapsule --build-launchers' to compile them."
        )
    return path


def compile_launcher(console=False):
    """Compile launcher.c to a pre-compiled binary.

    Returns the path to the compiled binary.
    """
    src = SRC_DIR / "launcher.c"
    if not src.exists():
        raise FileNotFoundError(f"launcher.c not found: {src}")

    LAUNCHERS_DIR.mkdir(parents=True, exist_ok=True)

    if sys.platform == "win32":
        # Native Windows: find gcc
        cc = shutil.which("gcc") or shutil.which("clang")
        if not cc:
            raise RuntimeError("No C compiler found (gcc/clang)")
        suffix = "console" if console else "gui"
        out = LAUNCHERS_DIR / f"launcher_win_x64_{suffix}.exe"
        flags = [cc, "-O2", "-s", "-std=c11", "-o", str(out), str(src),
                 "-lz", f"-I{SRC_DIR}", f"-I{SRC_DIR.parent / 'src'}"]
        if not console:
            flags.append("-mwindows")
        subprocess.run(flags, check=True, cwd=str(SRC_DIR))
    else:
        # Cross-compile for Windows via MinGW-w64
        cc = "x86_64-w64-mingw32-gcc"
        if not shutil.which(cc):
            # Fall back to native
            cc = shutil.which("gcc") or shutil.which("cc")
            if not cc:
                raise RuntimeError("No C compiler found")
            suffix = ""
            out = LAUNCHERS_DIR / f"launcher_{sys.platform}_x64"
            flags = [cc, "-O2", "-s", "-std=c11", "-o", str(out), str(src),
                     "-lz", f"-I{SRC_DIR}"]
        else:
            suffix = "console" if console else "gui"
            out = LAUNCHERS_DIR / f"launcher_win_x64_{suffix}.exe"
            flags = [cc, "-O2", "-s", "-std=c11", "-o", str(out), str(src),
                     "/usr/x86_64-w64-mingw32/lib/libz.a",
                     f"-I{SRC_DIR}",
                     f"-I/usr/x86_64-w64-mingw32/include",
                     f"-L/usr/x86_64-w64-mingw32/lib",
                     "-static-libgcc"]
            if not console:
                flags.append("-mwindows")
            else:
                flags.extend(["-lgdi32", "-luser32", "-lcomctl32"])
        subprocess.run(flags, check=True, cwd=str(SRC_DIR))

    print(f"  Compiled: {out} ({out.stat().st_size / 1024:.0f} KB)")
    return out


def build(project_zip, output_path, spec):
    """Stitch launcher + ZIP into final executable.

    If pre-compiled launcher exists, use it. Otherwise compile from source.
    """
    console = spec.get("console", False)
    target = spec.get("target", None)
    output = Path(output_path)

    # Get or compile launcher
    try:
        launcher = get_launcher_path(console=console, target=target)
    except FileNotFoundError:
        print("  Pre-compiled launcher not found, compiling from source...")
        launcher = compile_launcher(console=console)

    # Copy launcher + append ZIP + MAGIC + size
    shutil.copy2(launcher, output)

    with open(output, "ab") as f:
        f.write(project_zip)
        f.write(MAGIC)
        f.write(struct.pack("<I", len(project_zip)))

    if sys.platform != "win32":
        os.chmod(output, 0o755)

    size_kb = output.stat().st_size / 1024
    print(f"  Built: {output} ({size_kb:.0f} KB)")

    return output
