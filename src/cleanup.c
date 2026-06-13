/*
 * cleanup.c — 清理 C Launcher 初始化的临时文件
 *
 * 删除 launcher 在 %TEMP% (或 /tmp) 下创建的所有文件:
 *   _mf_env_ok      — 首次运行标记
 *   mf_XXXXXX/      — 解压的临时项目目录
 *
 * 编译:  gcc -o cleanup cleanup.c    (任何平台)
 *       cl cleanup.c                 (MSVC)
 * 运行:  ./cleanup                    (无参数, 自动扫描)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
  #define unlink  _unlink
  #define rmdir   _rmdir
  #define PATH_SEP '\\'
  #define PATH_SEP_STR "\\"
#else
  #include <unistd.h>
  #include <dirent.h>
  #include <sys/stat.h>
  #define PATH_SEP '/'
  #define PATH_SEP_STR "/"
#endif

#define MAX_PATH_LEN 1024

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

/* ── 主流程 ────────────────────────────────────────── */
int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  /* 中文不乱码 */
#endif
    /* 确定临时目录 */
    const char *tmp = NULL;
#ifdef _WIN32
    tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "C:\\Windows\\Temp";
#else
    tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
#endif

    printf("扫描临时目录: %s\n", tmp);
    int cleaned = 0;

#ifdef _WIN32
    /* Windows: FindFirstFile 遍历 */
    char pattern[MAX_PATH_LEN];
    snprintf(pattern, sizeof(pattern), "%s\\*", tmp);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        printf("无法打开目录: %s\n", tmp);
        return 1;
    }
    do {
        /* 匹配 _mf_env_ok 或 mf_XXXXXX */
        if (strncmp(fd.cFileName, "_mf_env_ok", 10) == 0) {
            char full[MAX_PATH_LEN];
            snprintf(full, sizeof(full), "%s\\%s", tmp, fd.cFileName);
            if (DeleteFileA(full)) {
                printf("删除文件: %s\n", full); cleaned++;
            } else {
                printf("删除失败: %s\n", full);
            }
        } else if (strncmp(fd.cFileName, "mf_", 3) == 0 &&
                   (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char full[MAX_PATH_LEN];
            snprintf(full, sizeof(full), "%s\\%s", tmp, fd.cFileName);
            printf("删除目录: %s\n", full);
            remove_dir(full);
            cleaned++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    /* macOS / Linux: opendir/readdir */
    DIR *d = opendir(tmp);
    if (!d) {
        printf("无法打开目录: %s\n", tmp);
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strncmp(ent->d_name, "_mf_env_ok", 10) == 0) {
            char full[MAX_PATH_LEN];
            snprintf(full, sizeof(full), "%s/%s", tmp, ent->d_name);
            if (unlink(full) == 0) {
                printf("删除文件: %s\n", full); cleaned++;
            }
        } else if (strncmp(ent->d_name, "mf_", 3) == 0) {
            char full[MAX_PATH_LEN];
            snprintf(full, sizeof(full), "%s/%s", tmp, ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("删除目录: %s\n", full);
                remove_dir(full);
                cleaned++;
            }
        }
    }
    closedir(d);
#endif

    printf("──────────────────────────────────\n");
    if (cleaned == 0) {
        printf("没有需要清理的文件。\n");
    } else {
        printf("已清理 %d 项。\n", cleaned);
    }
    printf("\n按任意键关闭窗口...");
    getchar();
    return 0;
}
