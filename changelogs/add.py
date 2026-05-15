#!/usr/bin/env python3

import argparse
import os
import sys
from pathlib import Path
from typing import Mapping, Optional, Sequence, TextIO

from changelogs import _lib


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Add a per-entry changelog file.")
    parser.add_argument("--section")
    parser.add_argument("--area")
    parser.add_argument(
        "--change",
        "--message",
        dest="change",
        help="Changelog body text (alias: --message).",
    )
    parser.add_argument("--change-file")
    return parser.parse_args(argv)


def read_change(args: argparse.Namespace, stdin: TextIO) -> Optional[str]:
    if args.change and args.change_file:
        raise _lib.UsageError("Use only one of --change/--message or --change-file.")
    if args.change_file == "-":
        return stdin.read()
    if args.change_file:
        return Path(args.change_file).read_text()
    return args.change


def resolve_section(args: argparse.Namespace, root: Path, stdin: TextIO) -> str:
    sections = list(_lib.load_sections(root))
    if args.section:
        _lib.validate_section(args.section, sections)
        return args.section
    _lib.ensure_tty(stdin, ["--section"])
    return _lib.prompt_choice("section", sections)


def resolve_area(args: argparse.Namespace, root: Path, stdin: TextIO) -> str:
    areas = list(_lib.load_areas(root))
    if args.area:
        _lib.validate_area(args.area, areas)
        return args.area
    _lib.ensure_tty(stdin, ["--area"])
    return _lib.prompt_choice("area", areas)


def resolve_change(args: argparse.Namespace, stdin: TextIO) -> str:
    change = read_change(args, stdin)
    if change is not None:
        return _lib.normalize_change(change)
    _lib.ensure_tty(stdin, ["--change"])
    return _lib.normalize_change(_lib.prompt_multiline("Enter the changelog entry"))


def add_entry(root: Path, section: str, area: str, change: str) -> Path:
    _lib.validate_section(section, _lib.load_sections(root))
    _lib.validate_area(area, _lib.load_areas(root))
    path = _lib.next_entry_path(root, section, area, change)
    path.write_text(_lib.normalize_change(change))
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
        path = add_entry(
            root,
            resolve_section(args, root, stdin),
            resolve_area(args, root, stdin),
            resolve_change(args, stdin),
        )
        print(path.relative_to(root), file=stdout)
        return 0
    except _lib.UsageError as error:
        print(error, file=stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
