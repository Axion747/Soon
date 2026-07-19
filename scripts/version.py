# Injects FW_VERSION into the build.
#
# Priority:
#   1. FW_VERSION environment variable  (set by GitHub Actions from the git tag)
#   2. `git describe`                   (local builds inside a git repo)
#   3. "0.0.0-dev"                      (fallback)
import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

version = os.environ.get("FW_VERSION", "").strip()

if not version:
    try:
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        version = ""

if not version:
    version = "0.0.0-dev"

version = version.lstrip("vV")

print(f"[version.py] Building firmware version: {version}")
env.Append(BUILD_FLAGS=[f'-DFW_VERSION=\\"{version}\\"'])
