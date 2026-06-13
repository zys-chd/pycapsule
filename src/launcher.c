/*
 * launcher.c — Pycapsule 通用 Python 启动器
 *
 * 预编译一次，所有项目通用。运行时从 _pycapsule_/config.txt 读取配置。
 * 无需 #include "config.h"，无需重编译。
 *
 * 流程:
 *   0. --cleanup 模式: 等 Python 退出 → 删临时目录
 *   1. 查找 Python（任意版本）
 *   2. 解压 ZIP → 临时目录
 *   3. 读 _pycapsule_/config.txt
 *   4. 验证 Python 版本 ≥ 配置要求
 *   5. 首次运行: 检查依赖 → pip 安装 → 写 _mf_env_ok
 *   6. pythonw.exe bootstrap.py
 *   7. _exit(0), 清理子进程等 Python 退出后删临时目录
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
  #define unlink  _unlink
  #define rmdir   _rmdir
  #define access  _access
  #define F_OK     0
  #define PATH_SEP '\\'
  #define PATH_SEP_STR "\\\\"

  /* ── 静默 popen/system（无黑框） ── */
  typedef struct { HANDLE hProcess, hThread, hRead; } _spipe_t;
  static _spipe_t _spipes[16]; static int _spipe_n = 0;

  static FILE *spopen(const char *cmd) {
      if (_spipe_n >= 16) return NULL;
      HANDLE hRead, hWrite;
      SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
      if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return NULL;
      SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
      STARTUPINFOA si = {sizeof(si)};
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdOutput = hWrite; si.hStdError = hWrite; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      PROCESS_INFORMATION pi = {0};
      char *cmdline = strdup(cmd);
      BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                               CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
      free(cmdline); CloseHandle(hWrite);
      if (!ok) { CloseHandle(hRead); return NULL; }
      CloseHandle(pi.hThread);
      _spipes[_spipe_n].hProcess = pi.hProcess;
      _spipes[_spipe_n].hRead = hRead;
      _spipe_n++;
      return _fdopen(_open_osfhandle((intptr_t)hRead, 0), "r");
  }

  static int spclose(FILE *f) {
      for (int i = 0; i < _spipe_n; i++) {
          if (_spipes[i].hRead != INVALID_HANDLE_VALUE) {
              fclose(f);
              WaitForSingleObject(_spipes[i].hProcess, INFINITE);
              DWORD ec; GetExitCodeProcess(_spipes[i].hProcess, &ec);
              CloseHandle(_spipes[i].hProcess);
              _spipes[i].hRead = INVALID_HANDLE_VALUE;
              return (int)ec;
          }
      }
      fclose(f); return -1;
  }

  static int ssys(const char *cmd) {
      STARTUPINFOA si = {sizeof(si)};
      PROCESS_INFORMATION pi = {0};
      char *cmdline = strdup(cmd);
      BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                               CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
      free(cmdline);
      if (!ok) return -1;
      CloseHandle(pi.hThread);
      WaitForSingleObject(pi.hProcess, INFINITE);
      DWORD ec; GetExitCodeProcess(pi.hProcess, &ec);
      CloseHandle(pi.hProcess);
      return (int)ec;
  }

  #undef popen
  #undef pclose
  #define popen(c,m)  spopen(c)
  #define pclose(f)   spclose(f)
  #define system(c)   ssys(c)
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

/* ── 全局配置（从 config.txt 加载） ─────────────────── */
#define MAX_PATH_LEN    1024

static char g_temp_dir[1024] = {0};
static char g_cfg_dir[MAX_PATH_LEN] = {0};   /* 临时目录路径 */
static char g_app_name[128]   = "Application"; /* 默认值，config 加载后覆盖 */
static char g_python_url[256] = "https://www.python.org/downloads/";
static int  g_py_min_major = 3, g_py_min_minor = 10;
static char g_zip_prefix[128] = "";
static char g_entry[256] = "main.py";

/* 依赖列表（从 config 加载） */
#define CFG_MAX_REQS 32
static char g_req_import[CFG_MAX_REQS][128];
static char g_req_pip[CFG_MAX_REQS][256];
static int  g_req_count = 0;

#ifdef _WIN32
/* ── 状态提示窗口（Unicode, 美观设计） ── */
static HWND _stat_wnd = NULL;
static HFONT _stat_font = NULL, _stat_font_title = NULL;
static HBRUSH _stat_bg = NULL;

static WCHAR *utf8_to_w(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    WCHAR *w = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static void show_status(const char *text) {
    WCHAR *wtext = utf8_to_w(text);
    if (_stat_wnd) {
        if (wtext) { SetWindowTextW(GetDlgItem(_stat_wnd, 101), wtext); free(wtext); }
        UpdateWindow(_stat_wnd);
        return;
    }
    _stat_bg = CreateSolidBrush(RGB(0xF5, 0xF6, 0xFA));

    WCHAR *wtitle = utf8_to_w(g_app_name);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = _stat_bg;
    wc.lpszClassName = L"PCStatusWnd";
    RegisterClassW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 440, wh = 170;
    _stat_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"PCStatusWnd",
        wtitle ? wtitle : L"", WS_POPUP | WS_BORDER,
        (sw - ww)/2, (sh - wh)/2, ww, wh, NULL, NULL, wc.hInstance, NULL);
    free(wtitle);

    _stat_font_title = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    _stat_font = CreateFontW(24, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

    CreateWindowExW(0, L"STATIC", L"\U0001F680", WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 30, 50, 50, _stat_wnd, (HMENU)99, wc.hInstance, NULL);
    SendMessageW(GetDlgItem(_stat_wnd, 99), WM_SETFONT, (WPARAM)_stat_font, TRUE);

    CreateWindowExW(0, L"STATIC", wtext ? wtext : L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
        75, 36, ww - 95, 50, _stat_wnd, (HMENU)101, wc.hInstance, NULL);
    SendMessageW(GetDlgItem(_stat_wnd, 101), WM_SETFONT, (WPARAM)_stat_font, TRUE);
    free(wtext);

    HFONT sub_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
        75, 80, ww - 95, 40, _stat_wnd, (HMENU)102, wc.hInstance, NULL);
    SendMessageW(GetDlgItem(_stat_wnd, 102), WM_SETFONT, (WPARAM)sub_font, TRUE);

    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        0, wh - 2, ww, 2, _stat_wnd, NULL, wc.hInstance, NULL);

    ShowWindow(_stat_wnd, SW_SHOW);
    UpdateWindow(_stat_wnd);
}

static void show_sub_status(const char *text) {
    if (!_stat_wnd) return;
    WCHAR *w = utf8_to_w(text);
    if (w) { SetWindowTextW(GetDlgItem(_stat_wnd, 102), w); free(w); }
    UpdateWindow(_stat_wnd);
}

static void hide_status(void) {
    if (_stat_wnd) {
        DestroyWindow(_stat_wnd); _stat_wnd = NULL;
        if (_stat_font) { DeleteObject(_stat_font); _stat_font = NULL; }
        if (_stat_font_title) { DeleteObject(_stat_font_title); _stat_font_title = NULL; }
        if (_stat_bg) { DeleteObject(_stat_bg); _stat_bg = NULL; }
    }
}

static void pump_messages(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
}
#endif

/* ── 运行时 ZIP 加载（从 exe 尾部读取） ──────────────── */

#define ZIP_MAGIC 0x505A4350  /* "PCZP" little-endian */

static unsigned char* load_zip(size_t *out_size) {
    *out_size = 0;

#ifdef _WIN32
    char my_path[MAX_PATH_LEN];
    if (!GetModuleFileNameA(NULL, my_path, sizeof(my_path))) return NULL;

    HANDLE h = CreateFileA(my_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    DWORD fsize = GetFileSize(h, NULL);
    if (fsize < 8) { CloseHandle(h); return NULL; }

    /* Read footer: [4B magic "PCZP"] [4B zip_size LE] */
    SetFilePointer(h, fsize - 8, NULL, FILE_BEGIN);
    unsigned char footer[8];
    DWORD br;
    if (!ReadFile(h, footer, 8, &br, NULL) || br != 8) {
        CloseHandle(h); return NULL;
    }

    if (*(uint32_t*)footer != ZIP_MAGIC) {
        CloseHandle(h); return NULL;
    }

    uint32_t zs = *(uint32_t*)(footer + 4);
    if (zs == 0 || (uint64_t)zs > (uint64_t)fsize - 8) {
        CloseHandle(h); return NULL;
    }

    unsigned char *zip = (unsigned char*)malloc(zs);
    if (!zip) { CloseHandle(h); return NULL; }

    SetFilePointer(h, fsize - 8 - zs, NULL, FILE_BEGIN);
    if (!ReadFile(h, zip, zs, &br, NULL) || br != zs) {
        free(zip); CloseHandle(h); return NULL;
    }
    CloseHandle(h);

    *out_size = zs;
    return zip;

#else
    /* macOS / Linux: read /proc/self/exe or argv[0] fallback */
    FILE *f = fopen("/proc/self/exe", "rb");
    if (!f) return NULL;

    fseeko(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 8) { fclose(f); return NULL; }

    fseeko(f, fsize - 8, SEEK_SET);
    unsigned char footer[8];
    if (fread(footer, 1, 8, f) != 8) { fclose(f); return NULL; }

    if (*(uint32_t*)footer != ZIP_MAGIC) { fclose(f); return NULL; }

    uint32_t zs = *(uint32_t*)(footer + 4);
    if (zs == 0 || (uint64_t)zs > (uint64_t)fsize - 8) {
        fclose(f); return NULL;
    }

    unsigned char *zip = (unsigned char*)malloc(zs);
    if (!zip) { fclose(f); return NULL; }

    fseeko(f, fsize - 8 - zs, SEEK_SET);
    if (fread(zip, 1, zs, f) != zs) { free(zip); fclose(f); return NULL; }
    fclose(f);

    *out_size = zs;
    return zip;
#endif
}

/* ── 宏 ────────────────────────────────────────────── */
#define ZIP_LOCAL_SIG   0x04034b50
#define ZIP_CENTRAL_SIG 0x02014b50
#define ZIP_EOCD_SIG    0x06054b50

/* ── 跨平台原生对话框 ─────────────────────────────── */
#ifdef _WIN32
/* UTF-8 → MessageBoxW */
static void win_msgbox(const char *title, const char *msg, UINT type) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, -1, NULL, 0);
    int tlen = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    if (wlen <= 0) wlen = 1;
    if (tlen <= 0) tlen = 1;
    WCHAR *wmsg = (WCHAR *)malloc(wlen * sizeof(WCHAR));
    WCHAR *wtit = (WCHAR *)malloc(tlen * sizeof(WCHAR));
    if (wmsg && wtit) {
        MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, wlen);
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtit, tlen);
        MessageBoxW(NULL, wmsg, wtit, MB_OK | type);
    }
    free(wmsg); free(wtit);
}
#define msgbox(t,m)       win_msgbox(t, m, MB_ICONINFORMATION)
#define msgbox_error(t,m) win_msgbox(t, m, MB_ICONERROR)

#else
static void msgbox(const char *title, const char *msg) {
#if defined(__APPLE__)
    char cmd[4096], escaped[2048];
    const char *s = msg; char *d = escaped;
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
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "zenity --info --title=\"%s\" --text=\"%s\" 2>/dev/null || "
             "kdialog --title \"%s\" --msgbox \"%s\" 2>/dev/null || "
             "xmessage -center \"%s\" 2>/dev/null",
             title, msg, title, msg, msg);
    if (system(cmd) != 0) fprintf(stderr, "[%s] %s\n", title, msg);
#endif
}

static void msgbox_error(const char *title, const char *msg) {
#if defined(__APPLE__)
    char cmd[4096], escaped[2048];
    const char *s = msg; char *d = escaped;
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
    if (system(cmd) != 0) fprintf(stderr, "[ERROR] %s: %s\n", title, msg);
#endif
}
#endif

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

/* ── 查找 Python（不检查版本，版本验证在 config 加载后） ── */
static int find_python(char *buf, size_t sz) {
#ifdef _WIN32
    char cand[512]; int have_cand = 0;

    /* 1. py 启动器 */
    { FILE *fp = popen("py -3 -c \"import sys; print(sys.executable, end='')\"", "r");
      if (fp) { if (fgets(cand, sizeof(cand), fp)) {
          size_t l = strlen(cand); while (l>0 && (cand[l-1]=='\n'||cand[l-1]=='\r')) cand[--l]=0;
          if (l>0 && access(cand, F_OK)==0) have_cand=1;
      } pclose(fp); } }

    /* 2. where python */
    if (!have_cand) { FILE *fp = popen("cmd.exe /c \"where python\"", "r");
      if (fp) { if (fgets(cand, sizeof(cand), fp)) {
          size_t l = strlen(cand); while (l>0 && (cand[l-1]=='\n'||cand[l-1]=='\r')) cand[--l]=0;
          if (l>0 && access(cand, F_OK)==0) have_cand=1;
      } pclose(fp); } }

    /* 3. 常见路径 */
    if (!have_cand) { const char *p[] = {
        "C:\\Python312\\python.exe","C:\\Python311\\python.exe","C:\\Python310\\python.exe",
        "C:\\Program Files\\Python312\\python.exe","C:\\Program Files\\Python311\\python.exe",
        "C:\\Program Files\\Python310\\python.exe", NULL};
      for (int i=0; p[i]; i++) if (access(p[i],F_OK)==0) {
          strncpy(cand,p[i],sizeof(cand)); cand[sizeof(cand)-1]=0; have_cand=1; break;
      } }

    if (!have_cand) return -1;
    strncpy(buf, cand, sz); buf[sz-1]=0;
    /* 同时存一份到 g_cfg_dir 做版本验证暂存 */
    return 0;
#else
    const char *cands[] = {"python3", "python", NULL};
    for (int i = 0; cands[i]; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "command -v %s 2>/dev/null", cands[i]);
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
#endif
}

/* ── 验证 Python 版本（config 加载后调用） ── */
static int verify_python_version(const char *python, int min_major, int min_minor,
                                  char *ver_out, size_t ver_sz) {
    char cmd[640];
    snprintf(cmd, sizeof(cmd), "\"%s\" --version", python);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    char ver[64] = {0};
    fread(ver, 1, sizeof(ver)-1, fp);
    pclose(fp);
    int major = 0, minor = 0;
    if (sscanf(ver, "Python %d.%d", &major, &minor) >= 2) {
        if (ver_out) snprintf(ver_out, ver_sz, "%d.%d", major, minor);
        if (major > min_major || (major == min_major && minor >= min_minor))
            return 0;
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
    size_t tlen = strlen(tmp);
    while (tlen > 0 && (tmp[tlen-1] == '/' || tmp[tlen-1] == '\\')) tlen--;
    char base[MAX_PATH_LEN];
    memcpy(base, tmp, tlen); base[tlen] = '\0';

#ifdef _WIN32
    char tpl[MAX_PATH_LEN];
    snprintf(tpl, sizeof(tpl), "%s\\pc_XXXXXX", base);
    if (!_mktemp_s(tpl, strlen(tpl)+1)) {
        CreateDirectoryA(tpl, NULL);
        strncpy(buf, tpl, sz); return 0;
    }
#else
    snprintf(buf, sz, "%s/pc_XXXXXX", base);
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
 * 运行时配置读取（flat key=value，从 _pycapsule_/config.txt）
 * ═══════════════════════════════════════════════════════ */

static int load_config(const char *tmpdir) {
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s/_pycapsule_/config.txt", tmpdir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    /* 保存配置路径 */
    snprintf(g_cfg_dir, sizeof(g_cfg_dir), "%s", tmpdir);

    /* 重置依赖计数 */
    g_req_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f) && g_req_count < CFG_MAX_REQS) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len] = 0;
        if (len == 0 || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "app_name") == 0) {
            strncpy(g_app_name, val, sizeof(g_app_name)-1);
        } else if (strcmp(key, "python_url") == 0) {
            strncpy(g_python_url, val, sizeof(g_python_url)-1);
        } else if (strcmp(key, "python_min_major") == 0) {
            g_py_min_major = atoi(val);
        } else if (strcmp(key, "python_min_minor") == 0) {
            g_py_min_minor = atoi(val);
        } else if (strcmp(key, "zip_prefix") == 0) {
            strncpy(g_zip_prefix, val, sizeof(g_zip_prefix)-1);
        } else if (strcmp(key, "entry") == 0) {
            strncpy(g_entry, val, sizeof(g_entry)-1);
        } else if (strcmp(key, "requirements_count") == 0) {
            g_req_count = atoi(val);
            if (g_req_count > CFG_MAX_REQS) g_req_count = CFG_MAX_REQS;
        }
    }
    fclose(f);

    /* 重新打开，读取 requirement 条目 */
    f = fopen(path, "r");
    if (!f) return 0; /* config loaded, reqs can be empty */

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1]=='\n' || line[len-1]=='\r')) line[--len] = 0;
        if (len == 0 || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        /* 匹配 req_<N>_import / req_<N>_pip */
        int idx;
        if (sscanf(key, "req_%d_import", &idx) == 1 && idx >= 0 && idx < g_req_count) {
            strncpy(g_req_import[idx], val, sizeof(g_req_import[idx])-1);
        } else if (sscanf(key, "req_%d_pip", &idx) == 1 && idx >= 0 && idx < g_req_count) {
            strncpy(g_req_pip[idx], val, sizeof(g_req_pip[idx])-1);
        }
    }
    fclose(f);
    return 0;
}

/* ═══════════════════════════════════════════════════════
 * 依赖检查（全部在 C 层完成）
 * ═══════════════════════════════════════════════════════ */

static int check_package(const char *python, const char *import_name) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "\"%s\" -c \"import %s\"", python, import_name);
    return system(cmd);
}

static void pip_install_one(const char *python, const char *pkg, int cur, int total) {
    char cmd[4096], prog[128];
    snprintf(prog, sizeof(prog), "正在安装 %s (%d/%d)...", pkg, cur, total);
    show_status(prog); pump_messages();
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -m pip install --quiet --disable-pip-version-check %s",
             python, pkg);
    system(cmd);
}

static int check_tkinter(const char *python) {
    return check_package(python, "tkinter");
}

/* ═══════════════════════════════════════════════════════
 * 主流程
 * ═══════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
#ifdef _WIN32
    /* ── 隐藏模式: --cleanup <tempdir> <pid> ── */
    if (argc >= 4 && strcmp(argv[1], "--cleanup") == 0) {
        DWORD pid = (DWORD)atoi(argv[3]);
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (h) {
            WaitForSingleObject(h, INFINITE);
            CloseHandle(h);
        }
        Sleep(500);
        remove_dir(argv[2]);
        return 0;
    }
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
#else
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
#endif
    atexit(do_cleanup);

    /* ── 0. 检查首次运行标记 ── */
    char stamp_path[MAX_PATH_LEN];
    {
        const char *td = getenv("TEMP");
        if (!td) td = getenv("TMP");
#ifdef _WIN32
        if (!td) td = "C:\\Windows\\Temp";
#else
        if (!td) td = "/tmp";
#endif
        snprintf(stamp_path, sizeof(stamp_path), "%s/_pc_env_ok", td);
    }
    int is_first_run = (access(stamp_path, F_OK) != 0);

    /* ── 1. 查找 Python（不检查版本） ── */
    char python[MAX_PATH_LEN] = {0};
    if (find_python(python, sizeof(python)) != 0) {
        msgbox_error("Pycapsule",
            "未找到 Python。\n\n"
            "请安装 Python 3.10 或更高版本：\n"
            "https://www.python.org/downloads/");
        return 1;
    }

    /* ── 2. 解压资源 ── */
    if (create_temp_dir(g_temp_dir, sizeof(g_temp_dir)) != 0) {
        msgbox_error("Pycapsule", "无法创建临时目录。");
        return 1;
    }
    {
        size_t zip_size = 0;
        unsigned char *zip_data = load_zip(&zip_size);
        if (!zip_data || zip_size == 0) {
            if (zip_data) free(zip_data);
            msgbox_error("Pycapsule", "无法读取应用数据。请重新构建。");
            return 1;
        }
        int extract_ok = (extract_zip(zip_data, zip_size, g_temp_dir) == 0);
        free(zip_data);
        if (!extract_ok) {
            msgbox_error("Pycapsule", "资源提取失败。请重新构建。");
            return 1;
        }
    }

    /* ── 3. 加载运行时配置 ── */
    if (load_config(g_temp_dir) != 0) {
        msgbox_error("Pycapsule",
            "无法读取应用配置 (_pycapsule_/config.txt)。\n"
            "请确认打包正确。");
        return 1;
    }

    /* ── 4. 验证 Python 版本 ── */
    {
        char found_ver[32] = "?";
        if (verify_python_version(python, g_py_min_major, g_py_min_minor,
                                  found_ver, sizeof(found_ver)) != 0) {
            char msg[1024];
            snprintf(msg, sizeof(msg),
                "Python 版本过低。\n\n"
                "找到: %s (版本 %s)\n"
                "需要: >= %d.%d\n\n"
                "请升级后重试：\n%s",
                python, found_ver,
                g_py_min_major, g_py_min_minor,
                g_python_url);
            msgbox_error(g_app_name, msg);
            return 1;
        }
    }

    /* ── 5. 环境检查（仅首次运行） ── */
    if (is_first_run) {
#ifndef _WIN32
        /* macOS/Linux: check tkinter */
        if (check_tkinter(python) != 0) {
            char msg[1024];
            snprintf(msg, sizeof(msg),
                "缺少 tkinter 组件。\n\n"
#if defined(__APPLE__)
                "请运行: brew install python-tk\n"
                "或使用系统自带 Python: /usr/bin/python3"
#else
                "请运行:\n"
                "  sudo apt install python3-tk    (Debian/Ubuntu)\n"
                "  sudo dnf install python3-tkinter (Fedora/RHEL)"
#endif
            );
            msgbox_error(g_app_name, msg);
            return 1;
        }
#endif

        /* 逐项检查 pip 依赖 */
#ifdef _WIN32
        show_status("正在检查运行环境..."); pump_messages();
#endif
        int missing_idx[CFG_MAX_REQS], missing_count = 0;
        for (int i = 0; i < g_req_count && missing_count < CFG_MAX_REQS; i++) {
#ifdef _WIN32
            { char prog[128];
              snprintf(prog, sizeof(prog),
                  "正在检查 %s (%d/%d)...", g_req_import[i], i+1, g_req_count);
              show_status(prog); pump_messages(); }
#endif
            if (check_package(python, g_req_import[i]) != 0) {
                missing_idx[missing_count++] = i;
            }
        }

        if (missing_count > 0) {
            for (int m = 0; m < missing_count; m++) {
                int idx = missing_idx[m];
                pip_install_one(python, g_req_pip[idx], m + 1, missing_count);
            }
            /* 验证安装 */
            int still_missing = 0;
            for (int i = 0; i < g_req_count; i++) {
                if (check_package(python, g_req_import[i]) != 0)
                    still_missing++;
            }
            if (still_missing > 0) {
#ifdef _WIN32
                hide_status();
#endif
                msgbox_error(g_app_name,
                    "部分依赖安装后仍不可用。\n请检查网络连接后重试。");
                return 1;
            }
        }

        /* 首次检查通过，写标记文件 */
#ifdef _WIN32
        hide_status();
#endif
        FILE *sf = fopen(stamp_path, "w");
        if (sf) { fprintf(sf, "ok\n"); fclose(sf); }
    }

    /* ── 6. 启动应用 ── */
    char bootstrap_path[MAX_PATH_LEN];
    snprintf(bootstrap_path, sizeof(bootstrap_path),
             "%s" PATH_SEP_STR "_pycapsule_" PATH_SEP_STR "bootstrap.py",
             g_temp_dir);

#ifdef _WIN32
    /* pythonw.exe — 无控制台 */
    char pyw[MAX_PATH_LEN];
    strncpy(pyw, python, sizeof(pyw)); pyw[sizeof(pyw)-1] = '\0';
    char *dot = strstr(pyw, "python.exe");
    if (dot) memcpy(dot + 6, "w.exe", 5);

    char cmd[8192];
    int pos = snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", pyw, bootstrap_path);
    for (int i = 1; i < argc && (size_t)pos < sizeof(cmd) - 1; i++) {
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " \"%s\"", argv[i]);
    }

    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof(si);
    CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                   NULL, g_temp_dir, &si, &pi);

    /* 清理子进程 */
    {
        char my_path[MAX_PATH_LEN];
        GetModuleFileNameA(NULL, my_path, sizeof(my_path));
        char cln_cmd[MAX_PATH_LEN + 256];
        snprintf(cln_cmd, sizeof(cln_cmd), "\"%s\" --cleanup \"%s\" %lu",
                 my_path, g_temp_dir, pi.dwProcessId);
        STARTUPINFOA csi = {0}; PROCESS_INFORMATION cpi = {0}; csi.cb = sizeof(csi);
        CreateProcessA(NULL, cln_cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &csi, &cpi);
        if (cpi.hProcess) CloseHandle(cpi.hProcess);
        if (cpi.hThread) CloseHandle(cpi.hThread);
    }

    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
    _exit(0);
#else
    int ret = system(cmd);
    (void)ret;
#endif

    do_cleanup();
    return 0;
}
