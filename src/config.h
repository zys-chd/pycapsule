/*
 * config.h — pycapsule 项目配置模板
 * ─────────────────────────────────────────────────
 * 复制此文件并修改以下字段即可适配你的项目。
 *
 * 打包: python pycapsule/pack.py
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ── 项目信息 ───────────────────────────────────── */
#define PROJECT_NAME        "我的工具"            /* 中文名（对话框标题）       */
#define PROJECT_NAME_EN     "My Tool"             /* 英文名（输出文件名）       */
#define PROJECT_VERSION     "1.0"
#define PROJECT_URL         "https://www.python.org/downloads/"

/* ── ZIP 前缀 ───────────────────────────────────── */
/* ZIP 包内所有文件的前缀目录名，应和 Python 包名一致 */
#define ZIP_PREFIX          "my_package"

/* ── Python 最低版本 ────────────────────────────── */
#define MIN_PYTHON_MAJOR    3
#define MIN_PYTHON_MINOR    10

/* ── 依赖包列表 ─────────────────────────────────── */
/* {import_name, pip_name, min_version}                  */
/* import_name:  Python "import X" 检测用                */
/* pip_name:     pip install 包名                        */
/* min_version:  版本约束 (如 ">=2.0")                    */
#define REQUIREMENTS_COUNT  3

static const struct {
    const char *import_name;
    const char *pip_name;
    const char *min_version;
} REQUIREMENTS[] = {
    {"numpy",      "numpy",      ">=2.0"},
    {"pandas",     "pandas",     ">=2.0"},
    {"matplotlib", "matplotlib", ">=3.8"},
    /* 添加你的依赖包 ... */
};

/* ── tkinter 安装提示（各平台） ──────────────────── */
/* 如果你的项目不用 tkinter（如 PyQt/CLI），忽略此节 */
#define TKINTER_WIN_MSG \
    "请重新运行 Python 安装程序，勾选 'tcl/tk and IDLE' 选项。"

#define TKINTER_MAC_MSG \
    "请运行: brew install python-tk\n" \
    "或使用系统自带 Python: /usr/bin/python3"

#define TKINTER_LINUX_MSG \
    "请运行:\n" \
    "  sudo apt install python3-tk    (Debian/Ubuntu)\n" \
    "  sudo dnf install python3-tkinter (Fedora/RHEL)"

#endif /* CONFIG_H */
