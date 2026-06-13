"""
Pycapsule CLI - python -m pycapsule entry point.

Usage:
    python -m pycapsule                      # Package current directory
    python -m pycapsule myapp.spec           # Package using spec file
    python -m pycapsule --gen-spec           # Generate default .spec
    python -m pycapsule --analyze-only       # Only scan imports
    python -m pycapsule --build-launchers    # Compile launcher binaries
"""

import sys
import argparse
from pathlib import Path


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser(
        prog="pycapsule",
        description="Pycapsule - tiny Python application packager",
    )

    parser.add_argument(
        "spec_file", nargs="?",
        help="Path to .spec JSON file"
    )
    parser.add_argument(
        "--gen-spec", action="store_true",
        help="Generate default .spec file and exit"
    )
    parser.add_argument(
        "--analyze-only", action="store_true",
        help="Scan imports and print results"
    )
    parser.add_argument(
        "--build-launchers", action="store_true",
        help="Compile launcher binaries for all platforms"
    )
    parser.add_argument(
        "--name", help="Override app name"
    )
    parser.add_argument(
        "--entry", help="Override entry point (e.g. src/main.py)"
    )
    parser.add_argument(
        "--console", action="store_true",
        help="Show console window (default: hidden)"
    )
    parser.add_argument(
        "--icon", help="Path to .ico file (not yet implemented)"
    )
    parser.add_argument(
        "-o", "--output", default=None,
        help="Output path (default: dist/<app_name>.exe)"
    )
    parser.add_argument(
        "--target", choices=["windows", "macos", "linux"],
        help="Target platform for cross-compilation"
    )

    args = parser.parse_args(argv)

    project_root = Path.cwd()

    # --analyze-only
    if args.analyze_only:
        from .analyzer import scan_imports
        imports = scan_imports(project_root)
        print("Detected imports:")
        for imp in imports:
            print(f"  - {imp}")
        return 0

    # --build-launchers
    if args.build_launchers:
        from .builder import compile_launcher
        print("Compiling launchers...")
        compile_launcher(console=False)  # GUI launcher
        compile_launcher(console=True)   # Console launcher
        print("Done.")
        return 0

    # --gen-spec
    if args.gen_spec:
        from .spec import generate_spec
        spec_path = args.spec_file or f"{project_root.name}.spec"
        generate_spec(project_root, spec_path)
        return 0

    # Package mode
    from .spec import load_spec, generate_spec
    from .packer import create_zip
    from .builder import build

    # Load or generate spec
    if args.spec_file:
        spec = load_spec(args.spec_file)
    else:
        print("No .spec file provided, auto-generating...")
        spec = generate_spec(project_root)

    # CLI overrides
    if args.name:
        spec["app_name"] = args.name
    if args.entry:
        spec["entry"] = args.entry
    if args.console:
        spec["console"] = True
    if args.icon:
        spec["icon"] = args.icon
    if args.target:
        spec["target"] = args.target

    if not spec.get("app_name"):
        spec["app_name"] = project_root.name

    app_name = spec["app_name"]

    # Output path
    if args.output:
        output = Path(args.output)
    else:
        dist_dir = project_root / "dist"
        dist_dir.mkdir(exist_ok=True)
        ext = ".exe" if sys.platform == "win32" else ""
        output = dist_dir / f"{app_name}{ext}"

    print(f"Packaging: {app_name}")
    print(f"  Entry: {spec['entry']}")
    print(f"  Console: {spec['console']}")

    # 1. Create ZIP
    zip_data = create_zip(project_root, spec)
    print(f"  ZIP: {len(zip_data) / 1024:.1f} KB")

    # 2. Build
    build(zip_data, output, spec)

    print(f"\nDone: {output}")
    return 0
