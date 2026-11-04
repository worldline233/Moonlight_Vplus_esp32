"""Verify the pinned checkout, then apply the reviewed ESP32 test6 patch."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    recipe = Path(__file__).resolve().parent
    source = Path(sys.argv[1]).resolve()
    record = json.loads((recipe / "baseline.json").read_text(encoding="utf-8"))

    def git(*args: str) -> str:
        return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()

    if git("rev-parse", "HEAD") != record["baseline_commit"]:
        raise SystemExit("Unexpected baseline commit")
    if git("status", "--porcelain"):
        raise SystemExit("Source checkout must be clean before applying the patch")
    for module in record["submodules"]:
        actual = subprocess.check_output(
            ["git", "-C", str(source / module["path"]), "rev-parse", "HEAD"], text=True
        ).strip()
        if actual != module["commit"]:
            raise SystemExit(f"Unexpected submodule revision: {module['path']}")
    patch = recipe / "esp32-test6.patch"
    if digest(patch) != record["patch_sha256"]:
        raise SystemExit("Patch checksum mismatch")
    git("apply", "--check", str(patch))
    git("apply", str(patch))
    for relative, expected in record["patched_files"].items():
        if digest(source / relative) != expected:
            raise SystemExit(f"Patched source checksum mismatch: {relative}")
    print(f"Verified baseline, {len(record['submodules'])} submodules and "
          f"{len(record['patched_files'])} patched files.")


if __name__ == "__main__":
    main()
