# Pycapsule

Tiny Python application packager — **50KB** C launcher, no interpreter bundling.

| | PyInstaller | Pycapsule |
|---|---|---|
| Output size | ~30-70 MB | **~80 KB** |
| Interpreter | Bundled | System Python |
| Dependencies | Bundled | Auto pip install on first run |
| Config | .spec Python script | .spec JSON |
| Import detection | Recursive hooks | AST scanner |

> Pycapsule packages only YOUR code. The target machine must have Python installed.
> Ideal for internal tools, developer utilities, and any scenario where Python is already present.

## Quick Start

```bash
pip install pycapsule

cd my_project
python -m pycapsule              # auto-detect + package → dist/my_project.exe
```

## Usage

```bash
# Package current directory (auto-generates .spec)
python -m pycapsule

# Generate .spec file for customization
python -m pycapsule --gen-spec

# Package with custom spec
python -m pycapsule myapp.spec

# Show console window (default: hidden)
python -m pycapsule --console

# Only analyze imports (dry run)
python -m pycapsule --analyze-only

# Cross-compile for Windows from WSL/Linux
python -m pycapsule --target windows
```

## .spec File (JSON)

```json
{
  "app_name": "MyApp",
  "entry": "main.py",
  "console": false,
  "python_min": "3.10",
  "requirements": {
    "numpy": ">=2.0",
    "pandas": ">=2.0"
  },
  "hidden_imports": ["scipy.special"],
  "exclude_dirs": ["__pycache__", ".git", ".venv"],
  "stdout_file": null,
  "stderr_file": null
}
```

## How It Works

```
┌─ Build time ─────────────────────┐
│  analyzer.py  →  scan imports    │
│  packer.py    →  create ZIP      │
│      injects _pycapsule_/        │
│      ├── config.txt              │
│      └── bootstrap.py            │
│  builder.py   →  stitch binary   │
│  ┌──────────────────────────┐    │
│  │ launcher.exe  (pre-built)│    │
│  │ [ZIP data]               │    │
│  │ "PCZP" + size (8 bytes)  │    │
│  └──────────────────────────┘    │
└──────────────────────────────────┘

┌─ Run time ───────────────────────┐
│  1. Find Python (py -3 → PATH)   │
│  2. Read ZIP from self (PCZP)    │
│  3. Extract to %TEMP%            │
│  4. Read _pycapsule_/config.txt  │
│  5. First run: pip install deps  │
│  6. pythonw.exe bootstrap.py     │
│  7. Self-cleanup on exit         │
└──────────────────────────────────┘
```

## Dev: Building Launchers

Pre-compiled launchers ship with the pip package. To rebuild from source:

```bash
# Windows GUI launcher (-mwindows)
x86_64-w64-mingw32-gcc -O2 -s -std=c11 \
  -I src -I /usr/x86_64-w64-mingw32/include \
  src/launcher.c -L /usr/x86_64-w64-mingw32/lib -lz \
  -mwindows -o pycapsule/launchers/launcher_win_x64_gui.exe

# Windows Console launcher
x86_64-w64-mingw32-gcc -O2 -s -std=c11 \
  -I src -I /usr/x86_64-w64-mingw32/include \
  src/launcher.c -L /usr/x86_64-w64-mingw32/lib -lz \
  -lgdi32 -luser32 -lcomctl32 \
  -o pycapsule/launchers/launcher_win_x64_console.exe
```

Or use the built-in builder:

```bash
python -m pycapsule --build-launchers
```

## Project Structure

```
pycapsule/                ← pip package
├── analyzer.py           AST import scanner
├── spec.py               .spec JSON handling
├── packer.py             ZIP creation + config injection
├── builder.py            Binary stitching
├── cli.py                CLI entry
├── launchers/            Pre-compiled binaries
├── bootstrap.py          Universal Python entry
└── templates/            .spec templates

src/                      ← C source
├── launcher.c            C launcher (runtime config, dep mgmt)
├── config.h              Template reference
└── cleanup.c             Manual cleanup tool
```

## License

MIT — see LICENSE file.
