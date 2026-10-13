#!/usr/bin/env python3
"""Validate and atomically install an HyperHEN plugin ELF."""
import argparse
import os
import shutil
import tempfile
from pathlib import Path

from inspect_plugin import inspect_elf


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("plugin", type=Path)
    parser.add_argument("--root", type=Path, default=Path("/data/HyperHEN/plugins"))
    args = parser.parse_args()
    if not args.plugin.is_file():
        parser.error(f"plugin does not exist: {args.plugin}")
    try:
        descriptor = inspect_elf(args.plugin)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    args.root.mkdir(parents=True, exist_ok=True)
    destination = args.root / f"{descriptor['id']}.elf"
    try:
        if args.plugin.resolve() == destination.resolve():
            print(destination)
            return 0
    except OSError:
        pass

    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="wb", dir=args.root, prefix=f".{descriptor['id']}.",
                suffix=".installing", delete=False) as temporary:
            temporary_path = Path(temporary.name)
            with args.plugin.open("rb") as source:
                shutil.copyfileobj(source, temporary)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_path, 0o755)
        os.replace(temporary_path, destination)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    print(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
