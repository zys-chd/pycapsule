#!/usr/bin/env python3
"""
pycapsule — Python 项目打包为独立 C 二进制

用法:
    cd your-project
    python pycapsule/pack.py               # 完整打包 + 编译
    python pycapsule/pack.py --zip-only    # 仅生成 resource.h
    python pycapsule/pack.py --compile     # 仅编译（需要已有 resource.h）

前置条件:
    - macOS:      xcode-select --install
    - Linux:      sudo apt install build-essential zlib1g-dev
    - Windows:    MinGW-w64 (winlibs.com) 或 Visual Studio Build Tools

移植步骤:
    1. git clone pycapsule 放到项目根目录
    2. 编辑 pycapsule/src/config.h
    3. 创建 bootstrap.py（项目启动入口）
    4. python pycapsule/pack.py
"""

import os
import sys
import struct
import zlib
import re
import subprocess
import shutil
from pathlib import Path

# pycapsule 所在目录（pack.py 的位置）
PYBOX_DIR = Path(__file__).parent.resolve()
# 用户项目根目录（运行 pack.py 时的当前目录）
PROJECT_ROOT = Path.cwd().resolve()
# C 源码目录
SRC_DIR = PYBOX_DIR / "src"

# ── 排除列表（用户可在项目根目录放 pycapsule-exclude.txt 覆盖）────
EXCLUDE_DIRS = {
    ".git", "__pycache__", ".pytest_cache",
    "build", "dist", "log", ".venv",
    ".Spotlight-V100", ".fseventsd", ".Trashes",
    ".TemporaryItems", "$RECYCLE.BIN",
    "pycapsule",  # pycapsule 自身
}

EXCLUDE_FILES = set()

# 用户自定义排除文件
_usr_exclude = PROJECT_ROOT / "pycapsule-exclude.txt"
if _usr_exclude.exists():
    with open(_usr_exclude) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                if line.endswith("/"):
                    EXCLUDE_DIRS.add(line[:-1])
                else:
                    EXCLUDE_FILES.add(line)

COMPRESS_LEVEL = 9


# ── 读取 config.h ──────────────────────────────────────
def read_config():
    config_h = SRC_DIR / "config.h"
    if not config_h.exists():
        print("[错误] 找不到 pycapsule/src/config.h")
        print("请从 pycapsule/src/config.h 模板复制并编辑。")
        sys.exit(1)

    content = config_h.read_text()
    zip_prefix = "my_package"
    out_name = "my_app"

    for line in content.splitlines():
        m = re.match(r'#define\s+(\w+)\s+"([^"]+)"', line)
        if m:
            key, val = m.group(1), m.group(2)
            if key == "ZIP_PREFIX":
                zip_prefix = val
            elif key == "PROJECT_NAME_EN":
                out_name = val.lower().replace(" ", "_")

    return zip_prefix, out_name


ZIP_PREFIX, OUTPUT_NAME = read_config()


def should_exclude(relpath: str) -> bool:
    parts = Path(relpath).parts
    for part in parts:
        if part in EXCLUDE_DIRS:
            return True
    fname = os.path.basename(relpath)
    if fname in EXCLUDE_FILES:
        return True
    if fname.startswith("._"):
        return True
    if fname.endswith(".spec"):
        return True
    return False


# ── ZIP 创建 ──────────────────────────────────────────
def create_zip():
    files = []
    for root, dirs, names in os.walk(PROJECT_ROOT):
        dirs[:] = [d for d in sorted(dirs) if d not in EXCLUDE_DIRS]
        for name in sorted(names):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, PROJECT_ROOT)
            if should_exclude(rel):
                continue
            files.append((full, rel))

    print(f"打包 {len(files)} 个文件 ...")

    entries = []; local_headers = []; offset = 0

    for full_path, rel_path in files:
        with open(full_path, "rb") as f:
            data = f.read()

        zip_name = ZIP_PREFIX + "/" + rel_path.replace("\\", "/")
        crc = zlib.crc32(data) & 0xFFFFFFFF

        if COMPRESS_LEVEL > 0:
            compressed = zlib.compress(data, COMPRESS_LEVEL)
            if len(compressed) < len(data):
                method = 8; comp_data = compressed[2:-4]
            else:
                method = 0; comp_data = data
        else:
            method = 0; comp_data = data

        comp_size = len(comp_data); uncomp_size = len(data)
        name_bytes = zip_name.encode("utf-8")

        local_header = bytearray()
        local_header += struct.pack("<I", 0x04034b50)
        local_header += struct.pack("<H", 20)
        local_header += struct.pack("<H", 0x0800)
        local_header += struct.pack("<H", method)
        local_header += struct.pack("<H", 0)
        local_header += struct.pack("<H", 0)
        local_header += struct.pack("<I", crc)
        local_header += struct.pack("<I", comp_size)
        local_header += struct.pack("<I", uncomp_size)
        local_header += struct.pack("<H", len(name_bytes))
        local_header += struct.pack("<H", 0)
        local_header += name_bytes

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
        cd_entry += struct.pack("<H", 0) * 3
        cd_entry += struct.pack("<I", 0)
        cd_entry += struct.pack("<I", offset)
        cd_entry += name_bytes

        entries.append((len(local_headers), cd_entry))
        local_headers.append(bytes(local_header) + comp_data)
        offset += len(local_headers[-1])

    result = bytearray()
    for lh in local_headers: result += lh
    cd_start = len(result)
    for _, cd in entries: result += cd
    cd_end = len(result)

    result += struct.pack("<I", 0x06054b50)
    result += struct.pack("<H", 0) * 2
    result += struct.pack("<H", len(entries)) * 2
    result += struct.pack("<I", cd_end - cd_start)
    result += struct.pack("<I", cd_start)
    result += struct.pack("<H", 0)

    print(f"  ZIP: {len(result) / 1024:.1f} KB  ({len(files)} files)")
    return bytes(result)


# ── 生成 resource.h ────────────────────────────────────
def generate_header(zip_data: bytes):
    output_path = SRC_DIR / "resource.h"
    lines = [
        "/* auto-generated by pycapsule pack.py */",
        f"/* {len(zip_data)} bytes */",
        "",
    ]
    data_lines = []
    for i in range(0, len(zip_data), 16):
        chunk = zip_data[i:i+16]
        data_lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("static const unsigned char resource_zip[] = {")
    lines.extend(data_lines)
    lines.append("};")
    lines.append("static const size_t resource_zip_size = sizeof(resource_zip);")
    with open(output_path, "w") as f:
        f.write("\n".join(lines))
    print(f"  resource.h: {output_path.stat().st_size / 1024:.0f} KB")


# ── 编译 ──────────────────────────────────────────────
def find_compiler():
    # WSL / Linux 交叉编译: 优先 MinGW 交叉编译器
    if sys.platform != "win32":
        if shutil.which("x86_64-w64-mingw32-gcc"):
            return "x86_64-w64-mingw32-gcc"
    if sys.platform == "win32":
        for cc in ["cl", "gcc", "clang"]:
            if shutil.which(cc): return cc
    else:
        candidates = ["cc", "gcc", "clang"]
        if sys.platform == "darwin":
            candidates = ["clang", "cc", "gcc"]
        for cc in candidates:
            if shutil.which(cc): return cc
    return None


def compile_launcher():
    cc = find_compiler()
    if not cc:
        print("[错误] 找不到 C 编译器\n")
        print("  macOS:              xcode-select --install")
        print("  Linux (Debian):     sudo apt install build-essential zlib1g-dev")
        print("  Linux (Fedora):     sudo dnf install gcc zlib-devel")
        print("  Windows (MinGW):    https://winlibs.com/ → 解压 → 加入 PATH")
        print("  Windows (MSVC):     VS Build Tools → Developer Command Prompt")
        return False

    src = SRC_DIR / "launcher.c"
    ext = ".exe" if sys.platform == "win32" else ""
    out = PYBOX_DIR / f"{OUTPUT_NAME}{ext}"

    if cc == "cl":
        flags = [cc, "/O2", "/DRESOURCE_H", f"/I{SRC_DIR}", str(src),
                 "/link", "/SUBSYSTEM:WINDOWS", "zlib.lib", f"/OUT:{out}"]
    elif cc.startswith("x86_64-w64-mingw32"):
        # 交叉编译 (WSL → Windows)
        flags = [cc, "-O2", "-s", "-std=c11", "-o", str(out), str(src),
                 "-lz", "-DRESOURCE_H", f"-I{SRC_DIR}", "-mwindows"]
    elif sys.platform == "win32":
        flags = [cc, "-O2", "-s", "-std=c11", "-o", str(out), str(src),
                 "-lz", "-DRESOURCE_H", f"-I{SRC_DIR}", "-mwindows"]
    else:
        flags = [cc, "-O2", "-s", "-o", str(out), str(src),
                 "-lz", "-DRESOURCE_H", f"-I{SRC_DIR}"]

    print(f"编译: {' '.join(str(f) for f in flags)}")
    result = subprocess.run(
        [str(f) for f in flags], cwd=SRC_DIR,
        capture_output=True, text=True,
        shell=(cc == "cl")
    )
    if result.returncode != 0:
        print(f"[错误] 编译失败:\n{result.stderr}")
        if "zlib" in (result.stderr + result.stdout).lower():
            print("\n  → 缺少 zlib。安装方法：\n"
                  "    macOS: (系统自带)\n"
                  "    Linux: sudo apt install zlib1g-dev\n"
                  "    Windows (MinGW): pacman -S mingw-w64-x86_64-zlib\n"
                  "    Windows (MSVC): vcpkg install zlib")
        return False

    if result.stderr.strip():
        print(f"[警告] {result.stderr.strip()}")

    size = os.path.getsize(out)
    print(f"[✓] {out.name}  ({size / 1024:.0f} KB)")
    if sys.platform != "win32":
        os.chmod(out, 0o755)
    return True


# ── 主流程 ────────────────────────────────────────────
def pack():
    print(f"pycapsule — {PROJECT_ROOT.name}")
    print()

    # 检查 config.h 是否已编辑
    import re as _re
    config_text = (SRC_DIR / "config.h").read_text()
    if 'PROJECT_NAME        "模型拟合工具"' in config_text:
        print("[!] 检测到 config.h 未修改（仍是模板默认值）。")
        print("    请编辑 pycapsule/src/config.h 后再运行。\n")
        print("    需要修改的字段：")
        print("      PROJECT_NAME       项目中文名")
        print("      PROJECT_NAME_EN     项目英文名（用作输出文件名）")
        print("      ZIP_PREFIX          Python 包目录名")
        print("      REQUIREMENTS[]      依赖包列表")
        return 1

    print("── 1. 资源打包 ──")
    zip_data = create_zip()

    print("\n── 2. resource.h ──")
    generate_header(zip_data)

    print("\n── 3. 编译 ──")
    if not compile_launcher():
        return 1

    ext = ".exe" if sys.platform == "win32" else ""
    print(f"\n[✓] 完成: pycapsule/{OUTPUT_NAME}{ext}")
    return 0


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser(description="pycapsule — Python project packager")
    p.add_argument("--zip-only", action="store_true")
    p.add_argument("--compile", action="store_true")
    args = p.parse_args()

    if args.compile:
        sys.exit(0 if compile_launcher() else 1)
    elif args.zip_only:
        zip_data = create_zip()
        generate_header(zip_data)
        sys.exit(0)
    else:
        sys.exit(pack())
