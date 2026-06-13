# pycapsule

> Pack Python projects into tiny self-contained binaries — 100× smaller than PyInstaller.  
> Zero console windows. Auto-installs missing dependencies.

```
PyInstaller:  74 MB  ──→  pycapsule:  < 1 MB   (100× smaller)
```

English | [中文](README_zh.md)

---

## How It Works

A tiny C launcher embeds your entire Python project as a compressed ZIP.  
When double-clicked, it silently checks the system Python, verifies every  
dependency, installs anything missing via `pip`, then launches your app —  
all without a terminal window.

```
your_app.exe  (double-click)
  │
  ├─ Check Python version   (C layer, no scripts)
  ├─ Check tkinter / GUI     (native error dialog if missing)
  ├─ Check every pip package (one by one, in C)
  ├─ Missing? → native dialog → pip install
  ├─ Extract project to temp → launch your Python app
  └─ Exit → auto-cleanup temp directory
```

**Native dialogs on every platform** — no console output, no terminal window.  
macOS uses `osascript`, Windows uses `MessageBox`, Linux uses `zenity`.

---

## Quick Start

### 1. Add pycapsule to your project

```bash
cd your-project
git clone https://github.com/yourname/pycapsule.git
```

### 2. Edit config

`pycapsule/src/config.h` — change 5 things:

```c
#define PROJECT_NAME        "My Tool"       // dialog title
#define PROJECT_NAME_EN     "My Tool"       // output binary name
#define ZIP_PREFIX          "my_package"    // Python package directory
#define REQUIREMENTS_COUNT  3
// pip dependencies (import_name, pip_name, version):
{ "numpy",      "numpy",      ">=2.0" },
{ "pandas",     "pandas",     ">=2.0" },
{ "matplotlib", "matplotlib", ">=3.8" },
```

### 3. Create bootstrap.py

In your project root, copy and edit `pycapsule/example/bootstrap.py`:

```python
import os, sys
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(script_dir))

from my_package.main import run
run()
```

### 4. Build

```bash
python pycapsule/pack.py
# → pycapsule/my_tool (macOS/Linux) or pycapsule/my_tool.exe (Windows)
```

---

## Build Requirements

| Platform | One-liner | Notes |
|----------|----------|-------|
| macOS | `xcode-select --install` | clang + zlib built-in |
| Linux | `sudo apt install build-essential zlib1g-dev` | gcc + zlib |
| Windows (MinGW) | [winlibs.com](https://winlibs.com/) → extract → add to PATH | then `pacman -S mingw-w64-x86_64-zlib` |
| Windows (MSVC) | Visual Studio Build Tools | manual: `cl /O2 /DRESOURCE_H ...` |

**Don't want zlib?** Drop [miniz.h](https://github.com/richgel999/miniz) into `pycapsule/src/`,  
change `#include <zlib.h>` → `#include "miniz.h"` in `launcher.c`, and compile without `-lz`.

---

## Custom Excludes

Create `pycapsule-exclude.txt` in your project root:

```
# directories (trailing /)
data/
models/checkpoints/

# files
.env
large_dataset.csv
```

---

## Project Structure

```
your-project/
├── pycapsule/                 ← git clone of pycapsule
│   ├── pack.py            ← build script
│   └── src/
│       ├── config.h       ← ✏️ your config
│       └── launcher.c     ← C source (don't touch)
├── bootstrap.py           ← ✏️ your entry point
├── pycapsule-exclude.txt      ← (optional)
└── your_package/          ← your Python code
```

---

## License

MIT
