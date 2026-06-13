/*
 * config.h — Pycapsule 配置模板（仅供参考）
 *
 * 注意：此文件不再被 launcher.c #include。
 * launcher.c 在运行时从 _pycapsule_/config.txt 读取配置。
 *
 * 打包时 pack.py 根据 .spec 文件生成 config.txt。
 * 此文件仅作文档参考和直接 hack 场景使用。
 */

#ifndef CONFIG_H
#define CONFIG_H

/* 如果你需要在编译时嵌入默认配置（非标准场景），
 * 可以定义以下宏，launcher.c 会作为 fallback 使用。
 * 标准用法不定义任何宏。 */

/* 默认应用名（config 加载前用于对话框标题） */
/* #define DEFAULT_APP_NAME "Pycapsule" */

#endif /* CONFIG_H */
