"""
pybox 示例 — bootstrap.py 模板

复制到你的项目根目录，修改启动入口即可。
C launcher 完成所有环境检测后调用此脚本。
"""

import os
import sys

# === 确保包可导入 ===
script_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(script_dir)
if parent_dir not in sys.path:
    sys.path.insert(0, parent_dir)
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)


def main():
    # 解析命令行参数
    csv_path = None
    for a in sys.argv[1:]:
        if not a.startswith("--"):
            csv_path = a

    try:
        # ==========================================
        # TODO: 替换为你的启动代码
        # ==========================================
        from my_package.main import run
        run(csv_path=csv_path)
        # ==========================================
    except Exception as e:
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
