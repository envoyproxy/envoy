#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  bazel run //tools/code_format:python_check -- -h
#
# alternatively, if you have the necessary python deps available
#
#  PYTHONPATH=. ./tools/code_format/python_check.py -h
#
# python requires: flake8, yapf
#

import argparse
import pathlib
import sys
from functools import cached_property
from typing import Iterable, List, Optional, Tuple

from flake8.main.application import Application as Flake8Application  # type:ignore

import yapf  # type:ignore

from tools.base import checker, utils

FLAKE8_CONFIG = '.flake8'
YAPF_CONFIG = '.style.yapf'

# TODO(phlax): add checks for:
#      - isort


class PythonChecker(checker.ForkingChecker):
    checks = ("flake8", "yapf")

    @property
    def diff_file_path(self) -> Optional[pathlib.Path]:
        return pathlib.Path(self.args.diff_file) if self.args.diff_file else None

    @cached_property
    def flake8_app(self) -> Flake8Application:
        flake8_app = Flake8Application()
        flake8_app.initialize(self.flake8_args)
        return flake8_app

    @property
    def flake8_args(self) -> Tuple[str, ...]:
        return ("--config", str(self.flake8_config_path), str(self.path))

    @property
    def flake8_config_path(self) -> pathlib.Path:
        return self.path.joinpath(FLAKE8_CONFIG)

    @property
    def recurse(self) -> bool:
        """Flag to determine whether to apply checks recursively"""
        return self.args.recurse

    @property
    def yapf_config_path(self) -> pathlib.Path:
        return self.path.joinpath(YAPF_CONFIG)

    @property
    def yapf_files(self) -> List[str]:
        return yapf.file_resources.GetCommandLineFiles(
            self.args.paths,
            recursive=self.recurse,
            exclude=yapf.file_resources.GetExcludePatternsForDir(str(self.path)))

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        super().add_arguments(parser)
        parser.add_argument(
            "--recurse",
            "-r",
            choices=["yes", "no"],
            default="yes",
            help="Recurse path or paths directories")
        parser.add_argument(
            "--diff-file", default=None, help="Specify the path to a diff file with fixes")

    def check_flake8(self) -> None:
        """Run flake8 on files and/or repo"""
        errors: List[str] = []
        with utils.buffered(stdout=errors, mangle=self._strip_lines):
            self.flake8_app.run_checks()
            self.flake8_app.report()
        if errors:
            self.error("flake8", errors)

    def check_yapf(self) -> None:
        """Run flake8 on files and/or repo"""
        for python_file in self.yapf_files:
            self.yapf_run(python_file)

    def on_check_run(self, check: str) -> None:
        if check not in self.failed and check not in self.warned:
            self.succeed(check, [check])

    def on_checks_complete(self) -> int:
        if self.diff_file_path and self.has_failed:
            result = self.subproc_run(["git", "diff", "HEAD"])
            self.diff_file_path.write_bytes(result.stdout)
        return super().on_checks_complete()

    def yapf_format(self, python_file: str) -> tuple:
        return yapf.yapf_api.FormatFile(
            python_file,
            style_config=str(self.yapf_config_path),
            in_place=self.fix,
            print_diff=not self.fix)

    def yapf_run(self, python_file: str) -> None:
        reformatted_source, encoding, changed = self.yapf_format(python_file)
        if not changed:
            return self.succeed("yapf", [python_file])
        if self.fix:
            return self.warn("yapf", [f"{python_file}: reformatted"])
        if reformatted_source:
            return self.warn("yapf", [f"{python_file}: diff\n{reformatted_source}"])
        self.error("yapf", [python_file])

    def _strip_line(self, line: str) -> str:
        return line[len(str(self.path)) + 1:] if line.startswith(f"{self.path}/") else line

    def _strip_lines(self, lines: Iterable[str]) -> List[str]:
        return [self._strip_line(line) for line in lines if line]


def main(*args: str) -> int:
    return PythonChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
