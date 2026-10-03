"""Assemble a local Windows distribution and record the exact input artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

base = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--qt-prefix", default=str(base / "dependencies/mingw64"))
parser.add_argument("--output-dir", type=Path, default=base / "dist")
args = parser.parse_args()
prefix = Path(args.qt_prefix)
dest = args.output_dir.resolve()
dest.mkdir(parents=True, exist_ok=True)
shutil.copytree(base / "assets", dest / "assets", dirs_exist_ok=True)
for source in [base / "build/source-navigator.exe", base / "core/target/release/sn-index.exe"]:
    shutil.copy2(source, dest / source.name)
compiler = Path("C:/msys64/mingw64")
for name in ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Core5Compat.dll", "Qt6PrintSupport.dll"]:
    shutil.copy2(prefix / "bin" / name, dest / name)
(dest / "platforms").mkdir(exist_ok=True)
for name in ["qwindows.dll", "qoffscreen.dll"]:
    shutil.copy2(prefix / "share/qt6/plugins/platforms" / name, dest / "platforms" / name)
(dest / "qt.conf").write_text("[Paths]\nPlugins=.\n", encoding="utf-8")
(dest / "translations").mkdir(exist_ok=True)
for language in ["it", "fr", "de"]:
    shutil.copy2(prefix / f"share/qt6/translations/qtbase_{language}.qm", dest / f"translations/qtbase_{language}.qm")
queue = list(dest.rglob("*.dll")) + list(dest.glob("*.exe"))
seen = set()
while queue:
    item = queue.pop()
    if item.name.lower() in seen:
        continue
    seen.add(item.name.lower())
    output = subprocess.check_output([str(compiler / "bin/objdump.exe"), "-p", str(item)], text=True, errors="replace")
    for name in re.findall(r"DLL Name:\s*(\S+)", output):
        target = dest / name
        source = next((folder / "bin" / name for folder in [prefix, compiler] if (folder / "bin" / name).exists()), None)
        if source and not target.exists():
            shutil.copy2(source, target)
            queue.append(target)
shutil.copytree(base.parent / "outputs", dest / "legacy", dirs_exist_ok=True)
shutil.copytree(base / "demo", dest / "demo", dirs_exist_ok=True, ignore=shutil.ignore_patterns(".sn-index"))
demo_cache = dest / "demo/.sn-index"
if demo_cache.exists():
    resolved_cache = demo_cache.resolve()
    if not resolved_cache.is_relative_to(dest.resolve()) or demo_cache.is_symlink():
        raise RuntimeError("Demo cache outside the distribution directory")
    shutil.rmtree(resolved_cache)
shutil.copytree(base / "themes", dest / "themes", dirs_exist_ok=True)
licenses = dest / "licenses"
licenses.mkdir(exist_ok=True)
for component in ["scintilla", "lexilla"]:
    shutil.copy2(base / "third_party" / component / "License.txt", licenses / f"{component}.txt")
shutil.copy2(base.parent / "COPYING", licenses / "Source-Navigator-4.5.txt")
for name in ["LICENSE", "THIRD_PARTY.md", "README.md"]:
    shutil.copy2(base / name, dest / name)
qt_licenses = prefix / "share/licenses/qt6-base"
if qt_licenses.exists():
    shutil.copytree(qt_licenses, licenses / "Qt", dirs_exist_ok=True)
for package_name in ["qt6-5compat", "qt6-translations", "gcc-libs", "winpthreads", "mingw-w64-libraries", "zlib", "zstd", "libpng", "freetype", "harfbuzz", "brotli", "pcre2", "icu", "glib2", "gettext-runtime", "libiconv", "md4c", "bzip2", "double-conversion", "libb2", "libjpeg-turbo", "graphite2"]:
    for directory in [prefix, compiler]:
        source = directory / "share/licenses" / package_name
        if source.exists():
            shutil.copytree(source, licenses / package_name, dirs_exist_ok=True)
metadata = json.loads(subprocess.check_output(["cargo", "metadata", "--locked", "--offline", "--manifest-path", str(base / "core/Cargo.toml"), "--format-version", "1"], text=True))
dependencies = []
for package in metadata["packages"]:
    dependencies.append({"name": package["name"], "version": package["version"], "license": package["license"], "source": package["source"]})
    location = Path(package["manifest_path"]).parent
    texts = list(location.glob("LICENSE*")) + list(location.glob("COPYING*"))
    for text in texts:
        if text.is_file():
            target = licenses / "Rust" / f"{package['name']}-{package['version']}"
            target.mkdir(parents=True, exist_ok=True)
            shutil.copy2(text, target / text.name)
(dest / "dependencies.json").write_text(json.dumps(dependencies, indent=2), encoding="utf-8")
manifest = []
for p in sorted(dest.rglob("*")):
    if p.is_file() and p.name != "manifest.json":
        manifest.append({"path": p.relative_to(dest).as_posix(), "bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()})
(dest / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
print(f"Distribution: {dest}; {len(manifest)} files")
