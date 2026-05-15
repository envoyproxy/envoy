#!/usr/bin/env python3

import argparse
import os
import sys
from pathlib import Path
from typing import Mapping, Optional, Sequence, TextIO

from changelogs import _lib


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Add a changelog area.")
    parser.add_argument("--area")
    parser.add_argument("--title")
    return parser.parse_args(argv)


def resolve_area(args: argparse.Namespace, stdin: TextIO) -> str:
    if args.area:
        _lib.validate_area_name(args.area)
        return args.area
    _lib.ensure_tty(stdin, ["--area"])
    while True:
        area = _lib.prompt_with_default("area")
        try:
            _lib.validate_area_name(area)
            return area
        except _lib.UsageError as error:
            print(error)


def resolve_title(args: argparse.Namespace, area: str, stdin: TextIO) -> str:
    if args.title is not None:
        return args.title
    return area


def add_area(root: Path, area: str, title: str) -> Path:
    path = _lib.areas_path(root)
    areas = _lib.load_areas(root)
    comments, _ = _lib.split_leading_comment_block(path.read_text())
    if area in areas:
        raise _lib.UsageError(f"Changelog area '{area}' already exists.")
    areas[area] = {"title": title}
    path.write_text(f"{comments}{_lib.dump_areas(areas)}")
    return path


def main(
        argv: Optional[Sequence[str]] = None,
        stdin: Optional[TextIO] = None,
        stdout: Optional[TextIO] = None,
        stderr: Optional[TextIO] = None,
        environ: Optional[Mapping[str, str]] = None) -> int:
    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    stderr = stderr or sys.stderr
    environ = environ or os.environ
    try:
        args = parse_args(argv)
        root = _lib.repo_root(environ)
        area = resolve_area(args, stdin)
        path = add_area(root, area, resolve_title(args, area, stdin))
        print(path.relative_to(root), file=stdout)
        return 0
    except _lib.UsageError as error:
        print(error, file=stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
