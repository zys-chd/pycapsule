/*
 * launcher.c — 通用 Python 项目 C 启动器
 *
 * 将 Python 项目 ZIP 嵌入二进制，运行时：
 *   1. 检测系统 Python 环境
 *   2. 逐项检查依赖（tkinter + pip 包）
 *   3. 缺失则自动安装
 *   4. 启动 Python 主程序
 *   5. 退出时清理临时目录
 *
 * 移植到新项目：只需修改 config.h
 * 编译：python pack.py
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
  #define popen   _popen
  #define pclose  _pclose
  #define unlink  _unlink
  #define rmdir   _rmdir
  #define access  _access
  #define F_OK     0
  #define PATH_SEP '\\'
  #define PATH_SEP_STR "\\"
#else
  #include <unistd.h>
  #include <signal.h>
  #include <sys/stat.h>
  #include <sys/wait.h>
  #include <dirent.h>
  #include <errno.h>
  #define PATH_SEP '/'
  #define PATH_SEP_STR "/"
#endif

#include <zlib.h>
#include "config.h"

/* ── 辅助宏 ─────────────────────────────────────────── */
#define _STR(x) #x
#define STR(x)  _STR(x)

/* ── 嵌入的资源 ZIP ────────────────────────────────── */
#ifdef RESOURCE_H
  #include "resource.h"
#else
  static const unsigned char resource_zip[] = {0};
  static const size_t   resource_zip_size = 0;
#endif

/* ── 全局变量 ──────────────────────────────────────── */
static char g_temp_dir[1024] = {0};

/* ── 宏 ────────────────────────────────────────────── */
#define ZIP_LOCAL_SIG   0x04034b50
#define ZIP_CENTRAL_SIG 0x02014b50
#define ZIP_EOCD_SIG    0x06054b50
#define MAX_PATH_LEN    1024

/* ── 跨平台原生对话框 ─────────────────────────────── */
static void msgbox(const char *title, const char *msg) {
#ifdef _WIN32
    MessageBoxA(NULL, msg, title, MB_OK | MB_ICONINFORMATION);
#elif defined(__APPLE__)
    char cmd[4096];
    /* 转义双引号和反斜杠 */
    char escaped[2048];
    const char *s = msg;
    char *d = escaped;
    while (*s && (size_t)(d - escaped) < sizeof(escaped) - 2) {
        if (*s == '\\') { *d++ = '\\'; *d++ = '\\'; }
        else if (*s == '"') { *d++ = '\\'; *d++ = '"'; }
        else *d++ = *s;
        s++;
    }
    *d = '\0';
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'display dialog \"%s\" with title \"%s\" "
             "buttons {\"确定\"} default button 1' 2>/dev/null",
             escaped, title);
    system(cmd);
#else
    /* Linux: 尝试 zenity / kdialog / xmessage */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "zenity --info --title=\"%s\" --text=\"%s\" 2>/dev/null || "
             "kdialog --title \"%s\" --msgbox \"%s\" 2>/dev/null || "
             "xmessage -center \"%s\" 2>/dev/null",
             title, msg, title, msg, msg);
    if (system(cmd) != 0) {
        /* 最后的 fallback */
        fprintf(stderr, "[%s] %s\n", title, msg);
    }
#endif
}

static void msgbox_error(const char *title, const char *msg) {
#ifdef _WIN32
    MessageBoxA(NULL, msg, title, MB_OK | MB_ICONERROR);
#elif defined(__APPLE__)
    char cmd[4096];
    char escaped[2048];
    const char *s = msg;
    char *d = escaped;
    while (*s && (size_t)(d - escaped) < sizeof(escaped) - 2) {
        if (*s == '\\') { *d++ = '\\'; *d++ = '\\'; }
        else if (*s == '"') { *d++ = '\\'; *d++ = '"'; }
        else *d++ = *s;
        s++;
    }
    *d = '\0';
    snprintf(cmd, sizeof(cmd),
             "osascript -e 'display dialog \"%s\" with title \"%s\" "
             "buttons {\"确定\"} default button 1 with icon stop' 2>/dev/null",
             escaped, title);
    system(cmd);
#else
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "zenity --error --title=\"%s\" --text=\"%s\" 2>/dev/null || "
             "xmessage -center \"%s\" 2>/dev/null",
             title, msg, msg);
    if (system(cmd) != 0) {
        fprintf(stderr, "[ERROR] %s: %s\n", title, msg);
    }
#endif
}

/* ── 小端序读取 ────────────────────────────────────── */
static inline uint16_t read16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ── 路径拼接 ──────────────────────────────────────── */
static void join_path(char *dst, size_t sz, const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    if (dlen >= sz) return;
    memcpy(dst, dir, dlen);
    dst[dlen] = PATH_SEP; dst[dlen + 1] = '\0';
    while (*name == '/' || *name == '\\') name++;
    const char *src = name;
    char *d = dst + dlen + 1;
    size_t remain = sz - dlen - 1;
    while (*src && remain > 1) {
        *d++ = (*src == '/' || *src == '\\') ? PATH_SEP : *src;
        src++; remain--;
    }
    *d = '\0';
}

/* ── 递归创建目录 ──────────────────────────────────── */
static int mkdir_p(const char *path) {
    char tmp[MAX_PATH_LEN];
    strncpy(tmp, path, MAX_PATH_LEN); tmp[MAX_PATH_LEN-1] = '\0';
    size_t len = strlen(tmp);
    while (len > 0 && (tmp[len-1] == '/' || tmp[len-1] == '\\')) tmp[--len] = '\0';
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            tmp[i] = '\0';
#ifdef _WIN32
            if (i > 2) CreateDirectoryA(tmp, NULL);
#else
            mkdir(tmp, 0755);
#endif
            tmp[i] = PATH_SEP;
        }
    }
#ifdef _WIN32
    CreateDirectoryA(tmp, NULL);
#else
    mkdir(tmp, 0755);
#endif
    return 0;
}

static void parent_dir(char *dst, size_t sz, const char *path) {
    strncpy(dst, path, sz); dst[sz-1] = '\0';
    char *last = strrchr(dst, PATH_SEP);
    if (!last) last = strrchr(dst, '/');
    if (last) *last = '\0';
}

/* ── ZIP 提取 ──────────────────────────────────────── */
static int extract_zip(const unsigned char *data, size_t size, const char *dest) {
    if (size < 22) return -1;

    /* 查找 EOCD */
    int64_t eocd = -1;
    int64_t ss = (int64_t)size - 22;
    if (ss < 0) ss = 0;
    for (int64_t i = ss; i >= 0; i--) {
        if (read32(data + i) == ZIP_EOCD_SIG) { eocd = i; break; }
    }
    if (eocd < 0) return -1;

    uint16_t total = read16(data + eocd + 10);
    uint32_t coff  = read32(data + eocd + 16);
    unsigned char *central = (unsigned char *)data + coff;
    int extracted = 0;

    for (uint16_t n = 0; n < total; n++) {
        if (read32(central) != ZIP_CENTRAL_SIG) break;
        uint16_t method      = read16(central + 10);
        uint32_t comp_sz     = read32(central + 20);
        uint32_t uncomp_sz   = read32(central + 24);
        uint16_t name_len    = read16(central + 28);
        uint16_t extra_len   = read16(central + 30);
        uint16_t comment_len = read16(central + 32);
        uint32_t local_off   = read32(central + 42);

        char name[512] = {0};
        uint16_t nl = name_len < 511 ? name_len : 511;
        memcpy(name, central + 46, nl);

        /* 目录 */
        if (name[nl-1] == '/' || name[nl-1] == '\\') {
            char fp[MAX_PATH_LEN]; join_path(fp, MAX_PATH_LEN, dest, name);
            mkdir_p(fp);
            central += 46 + name_len + extra_len + comment_len;
            continue;
        }

        unsigned char *loc = (unsigned char *)data + local_off;
        if (read32(loc) != ZIP_LOCAL_SIG) {
            central += 46 + name_len + extra_len + comment_len;
            continue;
        }
        uint16_t ln_len  = read16(loc + 26);
        uint16_t le_len  = read16(loc + 28);
        unsigned char *fd = loc + 30 + ln_len + le_len;

        char fp[MAX_PATH_LEN]; join_path(fp, MAX_PATH_LEN, dest, name);
        char pd[MAX_PATH_LEN]; parent_dir(pd, MAX_PATH_LEN, fp); mkdir_p(pd);

        FILE *out = fopen(fp, "wb");
        if (!out) { central += 46 + name_len + extra_len + comment_len; continue; }

        if (method == 0) {
            fwrite(fd, 1, uncomp_sz, out);
        } else if (method == 8) {
            z_stream strm = {0};
            if (inflateInit2(&strm, -MAX_WBITS) == Z_OK) {
                strm.next_in = fd; strm.avail_in = comp_sz;
                unsigned char buf[65536]; int ret;
                do {
                    strm.next_out = buf; strm.avail_out = sizeof(buf);
                    ret = inflate(&strm, Z_NO_FLUSH);
                    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) break;
                    fwrite(buf, 1, sizeof(buf) - strm.avail_out, out);
                } while (ret != Z_STREAM_END);
                inflateEnd(&strm);
            }
        }
        fclose(out); extracted++;
        central += 46 + name_len + extra_len + comment_len;
    }
    return extracted > 0 ? 0 : -1;
}

/* ── 查找 Python ───────────────────────────────────── */
static int find_python(char *buf, size_t sz) {
    const char *cands[] = {"python3", "python", NULL};
    for (int i = 0; cands[i]; i++) {
        char cmd[512];
#ifdef _WIN32
        snprintf(cmd, sizeof(cmd), "where %s 2>nul", cands[i]);
#else
        snprintf(cmd, sizeof(cmd), "command -v %s 2>/dev/null", cands[i]);
#endif
        FILE *fp = popen(cmd, "r");
        if (!fp) continue;
        if (fgets(buf, (int)sz, fp)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
            pclose(fp);
            if (len > 0) return 0;
        }
        pclose(fp);
    }
    return -1;
}

/* ── 临时目录 ──────────────────────────────────────── */
static int create_temp_dir(char *buf, size_t sz) {
    const char *tmp = getenv("TMPDIR");
    if (!tmp) tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
#ifdef _WIN32
    if (!tmp) tmp = "C:\\Windows\\Temp";
#else
    if (!tmp) tmp = "/tmp";
#endif
    /* 去尾斜杠 */
    size_t tlen = strlen(tmp);
    while (tlen > 0 && (tmp[tlen-1] == '/' || tmp[tlen-1] == '\\')) tlen--;
    char base[MAX_PATH_LEN];
    memcpy(base, tmp, tlen); base[tlen] = '\0';

#ifdef _WIN32
    char tpl[MAX_PATH_LEN];
    snprintf(tpl, sizeof(tpl), "%s\\mf_XXXXXX", base);
    if (!_mktemp_s(tpl, strlen(tpl)+1)) {
        CreateDirectoryA(tpl, NULL);
        strncpy(buf, tpl, sz); return 0;
    }
#else
    snprintf(buf, sz, "%s/mf_XXXXXX", base);
    if (mkdtemp(buf)) return 0;
#endif
    return -1;
}

/* ── 递归删除目录 ──────────────────────────────────── */
static void remove_dir(const char *path) {
#ifdef _WIN32
    char search[MAX_PATH_LEN];
    snprintf(search, sizeof(search), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search, &fd);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryA(path); return; }
    do {
        if (strcmp(fd.cFileName, ".")==0 || strcmp(fd.cFileName, "..")==0) continue;
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s\\%s", path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_dir(full);
        else DeleteFileA(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h); RemoveDirectoryA(path);
#else
    DIR *d = opendir(path);
    if (!d) { rmdir(path); return; }
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".")==0 || strcmp(ent->d_name, "..")==0) continue;
        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        struct stat st;
        if (stat(full, &st)==0 && S_ISDIR(st.st_mode)) remove_dir(full);
        else unlink(full);
    }
    closedir(d); rmdir(path);
#endif
}

/* ── 信号清理 ──────────────────────────────────────── */
static void do_cleanup(void) {
    if (g_temp_dir[0]) { remove_dir(g_temp_dir); g_temp_dir[0] = '\0'; }
}
#ifdef _WIN32
static BOOL WINAPI ctrl_handler(DWORD t) { (void)t; do_cleanup(); ExitProcess(1); return TRUE; }
#else
static void sig_handler(int s) { (void)s; do_cleanup(); _exit(1); }
#endif

/* ═══════════════════════════════════════════════════════
 * 依赖检查（全部在 C 层完成，不依赖 bootstrap.py）
 * ═══════════════════════════════════════════════════════ */

/* 运行 python 命令，返回 exit code */
static int run_py_cmd(const char *python, const char *py_code, char *out, size_t out_sz) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "\"%s\" -c \"%s\" 2>&1", python, py_code);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    if (out && out_sz > 0) {
        size_t n = fread(out, 1, out_sz - 1, fp);
        if (n > 0) out[n] = '\0';
        else out[0] = '\0';
    }
    int ret = pclose(fp);
#ifdef _WIN32
    return ret;
#else
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
#endif
}

/* 检查 pip 包是否已安装 */
static int check_package(const char *python, const char *import_name) {
    char code[512], out[256] = {0};
    snprintf(code, sizeof(code), "import %s", import_name);
    return run_py_cmd(python, code, out, sizeof(out));
}

/* pip install 包列表 */
static int pip_install(const char *python, const char *packages) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -m pip install --quiet --disable-pip-version-check %s 2>&1",
             python, packages);
    int ret = system(cmd);
#ifdef _WIN32
    return ret;
#else
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
#endif
}

/* 检查 Python 版本 */
static int check_python_version(const char *python) {
    char code[256];
    snprintf(code, sizeof(code),
             "import sys; sys.exit(0 if sys.version_info >= (%d,%d) else 1)",
             MIN_PYTHON_MAJOR, MIN_PYTHON_MINOR);
    return run_py_cmd(python, code, NULL, 0);
}

/* 检查 tkinter */
static int check_tkinter(const char *python) {
    return check_package(python, "tkinter");
}

/* ═══════════════════════════════════════════════════════
 * 主流程
 * ═══════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
#endif
    atexit(do_cleanup);

    /* ── 1. 查找 Python ── */
    char python[MAX_PATH_LEN] = {0};
    if (find_python(python, sizeof(python)) != 0) {
        msgbox_error(PROJECT_NAME,
            "未找到 Python。\n\n"
            "请安装 Python " STR(MIN_PYTHON_MAJOR) "." STR(MIN_PYTHON_MINOR) " 或更高版本：\n"
            PROJECT_URL);
        return 1;
    }

    /* ── 2. 检查 Python 版本 ── */
    if (check_python_version(python) != 0) {
        char msg[1024];
        snprintf(msg, sizeof(msg),
            "Python 版本过低。\n\n"
            "需要 Python >= " STR(MIN_PYTHON_MAJOR) "." STR(MIN_PYTHON_MINOR) "。\n"
            "请升级后重试：\n" PROJECT_URL);
        msgbox_error(PROJECT_NAME, msg);
        return 1;
    }

    /* ── 3. 创建临时目录、解压资源 ── */
    if (create_temp_dir(g_temp_dir, sizeof(g_temp_dir)) != 0) {
        msgbox_error(PROJECT_NAME, "无法创建临时目录。");
        return 1;
    }
    if (resource_zip_size == 0 || extract_zip(resource_zip, resource_zip_size, g_temp_dir) != 0) {
        msgbox_error(PROJECT_NAME, "资源提取失败。请重新构建。");
        return 1;
    }

    /* ── 4. 检查 tkinter ── */
    if (check_tkinter(python) != 0) {
#ifdef _WIN32
        const char *guide = TKINTER_WIN_MSG;
#elif defined(__APPLE__)
        const char *guide = TKINTER_MAC_MSG;
#else
        const char *guide = TKINTER_LINUX_MSG;
#endif
        char msg[1024];
        snprintf(msg, sizeof(msg),
            "缺少 tkinter 组件。\n\n%s", guide);
        msgbox_error(PROJECT_NAME, msg);
        return 1;
    }

    /* ── 5. 检查 pip 依赖 ── */
    char missing[2048] = {0};
    int missing_count = 0;
    for (int i = 0; i < REQUIREMENTS_COUNT; i++) {
        if (check_package(python, REQUIREMENTS[i].import_name) != 0) {
            if (missing_count > 0) strcat(missing, " ");
            strcat(missing, REQUIREMENTS[i].pip_name);
            missing_count++;
        }
    }

    /* ── 6. 安装缺失依赖 ── */
    if (missing_count > 0) {
        /* 提示用户 */
        char msg_text[3072];
        snprintf(msg_text, sizeof(msg_text),
            "首次运行，需要安装以下依赖：\n\n"
            "  %s\n\n"
            "点击「确定」开始安装（需要网络连接）。",
            missing);
#ifdef _WIN32
        int answer = MessageBoxA(NULL, msg_text, PROJECT_NAME " — 依赖安装",
                                 MB_OKCANCEL | MB_ICONINFORMATION);
        if (answer != IDOK) return 0;
#else
        /* macOS/Linux 用 osascript/zenity 询问 */
        msgbox(PROJECT_NAME " — 依赖安装", msg_text);
#endif

        if (pip_install(python, missing) != 0) {
            char err[1024];
            snprintf(err, sizeof(err),
                "依赖安装失败。\n\n"
                "请手动运行以下命令后重试：\n"
                "  pip install %s", missing);
            msgbox_error(PROJECT_NAME, err);
            return 1;
        }

        /* 验证安装 */
        int still_missing = 0;
        for (int i = 0; i < REQUIREMENTS_COUNT; i++) {
            if (check_package(python, REQUIREMENTS[i].import_name) != 0)
                still_missing++;
        }
        if (still_missing > 0) {
            msgbox_error(PROJECT_NAME,
                "部分依赖安装后仍不可用。\n请检查网络连接后重试。");
            return 1;
        }
    }

    /* ── 7. 启动应用 ── */
    /* 构建命令行: python3 <tmp>/ZIP_PREFIX/bootstrap.py [args...] */
    char bootstrap_path[MAX_PATH_LEN];
    snprintf(bootstrap_path, sizeof(bootstrap_path),
             "%s" PATH_SEP_STR ZIP_PREFIX PATH_SEP_STR "bootstrap.py",
             g_temp_dir);

    char cmd[8192];
    int pos = snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", python, bootstrap_path);
    for (int i = 1; i < argc && (size_t)pos < sizeof(cmd) - 1; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " \"%s\"", argv[i]);
    }

#ifdef _WIN32
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof(si);
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, g_temp_dir, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
#else
    int ret = system(cmd);
    (void)ret;
#endif

    do_cleanup();
    return 0;
}
