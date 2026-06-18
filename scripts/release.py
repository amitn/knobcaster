#!/usr/bin/env python3
"""Cut the next release: bump the latest vMAJOR.MINOR.PATCH git tag and push it.

Pushing a `v*` tag triggers the Release + Web Flasher GitHub workflows
(firmware.bin asset + the web flasher on Pages).

Usage:
    uv run python scripts/release.py [patch|minor|major] [--dry-run]
        patch (default) -> v0.1.3 -> v0.1.4
        minor           -> v0.1.3 -> v0.2.0
        major           -> v0.1.3 -> v1.0.0
        --dry-run       -> print the next version, tag nothing
"""
import re
import subprocess
import sys

args = [a for a in sys.argv[1:] if a != "--dry-run"]
dry = "--dry-run" in sys.argv
level = args[0] if args else "patch"
if level not in ("patch", "minor", "major"):
    sys.exit(f"usage: release.py [patch|minor|major] [--dry-run] (got {level!r})")


def git(*a):
    return subprocess.run(["git", *a], check=True, text=True,
                          capture_output=True).stdout.strip()


tags = git("tag", "--list", "v[0-9]*").splitlines()
cur = (0, 0, 0)
for t in tags:
    m = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)", t.strip())
    if m:
        cur = max(cur, tuple(int(x) for x in m.groups()))

mj, mn, pa = cur
if level == "major":
    mj, mn, pa = mj + 1, 0, 0
elif level == "minor":
    mn, pa = mn + 1, 0
else:
    pa += 1
new = f"v{mj}.{mn}.{pa}"

print(f"v{cur[0]}.{cur[1]}.{cur[2]} -> {new}  ({level})")
if new in tags:
    sys.exit(f"tag {new} already exists")
if dry:
    print("(dry run — nothing tagged)")
    sys.exit(0)

if git("status", "--porcelain"):
    print("note: working tree has uncommitted changes; the tag points at HEAD as-is")
if git("rev-parse", "HEAD") != git("rev-parse", "origin/main"):
    print("note: HEAD differs from origin/main — the tag carries your local commits; "
          "consider `git push` first so main matches the release")

git("tag", new)
subprocess.run(["git", "push", "origin", new], check=True)
print(f"pushed {new} -> Release + Web Flasher workflows triggered")
