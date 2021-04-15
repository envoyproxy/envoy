#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  bazel run //tools/code_format:python_check -- -h
#
# alternatively, if you have the necessary python deps available
#
#  ./tools/code_format/python_check.py -h
#
# python requires: flake8, yapf
#

import os
import sys
from functools import cached_property

from flake8.main.application import Application as Flake8Application

import yapf

from tools.base import checker, utils

FLAKE8_CONFIG = '.flake8'
YAPF_CONFIG = '.style.yapf'

# TODO(phlax): add checks for:
#      - isort


class PythonChecker(checker.ForkingChecker):
    checks = ("flake8", "yapf")

    @property
    def diff_file_path(self) -> str:
        return self.args.diff_file

    @cached_property
    def flake8_app(self) -> Flake8Application:
        flake8_app = Flake8Application()
        flake8_app.initialize(self.flake8_args)
        return flake8_app

    @property
    def flake8_args(self) -> list:
        return ["--config", self.flake8_config_path, self.path]

    @property
    def flake8_config_path(self) -> str:
        return os.path.join(self.path, FLAKE8_CONFIG)

    @property
    def recurse(self) -> bool:
        """Flag to determine whether to apply checks recursively"""
        return self.args.recurse

    @property
    def yapf_config_path(self) -> str:
        return os.path.join(self.path, YAPF_CONFIG)

    @property
    def yapf_files(self):
        return yapf.file_resources.GetCommandLineFiles(
            self.args.paths,
            recursive=self.recurse,
            exclude=yapf.file_resources.GetExcludePatternsForDir(self.path))

    def add_arguments(self, parser) -> None:
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
        errors = []
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
            self.succeed(check, [f"[CHECKS:{self.name}] {check}: success"])

    def on_checks_complete(self) -> int:
        if self.diff_file_path and self.has_failed:
            result = self.fork(["git", "diff", "HEAD"])
            with open(self.diff_file_path, "wb") as f:
                f.write(result.stdout)
        return super().on_checks_complete()

    def yapf_format(self, python_file: str) -> tuple:
        return yapf.yapf_api.FormatFile(
            python_file,
            style_config=self.yapf_config_path,
            in_place=self.fix,
            print_diff=not self.fix)

    def yapf_run(self, python_file: str) -> None:
        reformatted_source, encoding, changed = self.yapf_format(python_file)
        if not changed:
            return self.succeed("yapf", [f"{python_file}: success"])
        if self.fix:
            return self.warn("yapf", [f"{python_file}: reformatted"])
        if reformatted_source:
            return self.warn("yapf", [reformatted_source])
        self.error("yapf", [python_file])

    def _strip_line(self, line) -> str:
        return line[len(self.path) + 1:] if line.startswith(f"{self.path}/") else line

    def _strip_lines(self, lines) -> list:
        return [self._strip_line(line) for line in lines if line]


def main(*args: list) -> None:
    return PythonChecker(*args).run_checks()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
