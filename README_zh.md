# pybox

> 将 Python 项目打包为轻量独立二进制文件 — 比 PyInstaller 小 100 倍。  
> 无命令行窗口。自动安装缺失依赖。

```
PyInstaller:  74 MB  ──→  pybox:  < 1 MB   （缩小 100 倍）
```

[English](README.md) | 中文

---

## 原理

一个微型 C 启动器把你的 Python 项目压缩成 ZIP 嵌入二进制。双击运行时，静默检测系统 Python 环境，逐项验证依赖，缺失则自动 `pip install`，然后启动你的应用——全程无终端窗口。

```
your_app.exe  双击运行
  │
  ├─ 检查 Python 版本     （C 层完成，无需脚本）
  ├─ 检查 tkinter / GUI    （缺失时弹出原生对话框）
  ├─ 逐项检查 pip 依赖包   （C 层逐一验证）
  ├─ 有缺失？→ 原生对话框 → pip install
  ├─ 解压项目到临时目录 → 启动 Python 应用
  └─ 退出 → 自动清理临时目录
```

**各平台原生对话框** — 无命令行输出，无终端窗口。  
macOS 使用 `osascript`，Windows 使用 `MessageBox`，Linux 使用 `zenity`。

---

## 快速开始

### 1. 添加到你的项目

```bash
cd your-project
git clone https://github.com/zys-chd/pybox.git
```

### 2. 编辑配置

打开 `pybox/src/config.h`，改 5 个地方：

```c
#define PROJECT_NAME        "我的工具"       // 对话框标题
#define PROJECT_NAME_EN     "My Tool"        // 输出文件名
#define ZIP_PREFIX          "my_package"     // Python 包目录名
#define REQUIREMENTS_COUNT  3
// pip 依赖列表（import 名, pip 包名, 版本约束）：
{ "numpy",      "numpy",      ">=2.0" },
{ "pandas",     "pandas",     ">=2.0" },
{ "matplotlib", "matplotlib", ">=3.8" },
```

### 3. 创建启动脚本

在项目根目录创建 `bootstrap.py`（参考 `pybox/example/bootstrap.py`）：

```python
import os, sys
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(script_dir))

from my_package.main import run
run()
```

### 4. 打包

```bash
python pybox/pack.py
# → pybox/my_tool (macOS/Linux) 或 pybox/my_tool.exe (Windows)
```

---

## 编译环境

| 平台 | 安装命令 | 备注 |
|------|---------|------|
| macOS | `xcode-select --install` | clang + zlib 系统自带 |
| Linux | `sudo apt install build-essential zlib1g-dev` | gcc + zlib |
| Windows (MinGW) | [winlibs.com](https://winlibs.com/) → 解压 → 加入 PATH | 然后 `pacman -S mingw-w64-x86_64-zlib` |
| Windows (MSVC) | Visual Studio Build Tools | 手动 `cl /O2 /DRESOURCE_H ...` |

**不想装 zlib？** 下载 [miniz.h](https://github.com/richgel999/miniz) 放到 `pybox/src/`，  
把 `launcher.c` 中 `#include <zlib.h>` 改为 `#include "miniz.h"`，编译时去掉 `-lz` 即可。

---

## 自定义排除文件

在项目根目录创建 `pybox-exclude.txt`：

```
# 排除目录（以 / 结尾）
data/
models/checkpoints/

# 排除文件
.env
large_dataset.csv
```

---

## 项目结构

```
your-project/
├── pybox/                 ← git clone 的 pybox
│   ├── pack.py            ← 打包脚本
│   └── src/
│       ├── config.h       ← ✏️ 你的配置
│       └── launcher.c     ← C 源码（不用改）
├── bootstrap.py           ← ✏️ 你的启动入口
├── pybox-exclude.txt      ← （可选）排除列表
└── your_package/          ← 你的 Python 代码
```

---

## License

MIT
