#!/usr/bin/env python3

# usage information:
#
#   bazel run //tools/testing:all_pytests -- -h
#
# requires: bazel
#

import os
import sys
from functools import cached_property

from tools.base import checker, runner


class PytestChecker(checker.BazelChecker):
    checks = ("pytests",)

    @property
    def cov_enabled(self) -> bool:
        return bool(self.args.cov_collect or self.args.cov_html)

    @property
    def cov_html(self) -> str:
        return self.args.cov_html

    @property
    def cov_path(self):
        return self.args.cov_collect or os.path.abspath(".coverage-envoy")

    @property
    def pytest_bazel_args(self):
        return (
            [f"--cov-collect", self.cov_path]
            if self.cov_enabled
            else [])

    @cached_property
    def pytest_targets(self) -> set:
        return set(target for target in self.bazel.query("tools/...") if ":pytest_" in target)

    def add_arguments(self, parser):
        super().add_arguments(parser)
        parser.add_argument(
            "--cov-collect",
            default=None,
            help="Specify a path to collect coverage with")
        parser.add_argument(
            "--cov-html",
            default=None,
            help="Specify a path to collect html coverage with")

    def check_pytests(self) -> int:
        for target in self.pytest_targets:
            try:
                self.bazel.run(target, *self.pytest_bazel_args)
                self.succeed("pytests", [target])
            except runner.BazelRunError:
                self.error("pytests", [f"{target} failed"])

    def on_checks_begin(self):
        if self.cov_path and os.path.exists(self.cov_path):
            os.unlink(self.cov_path)

    def on_checks_complete(self):
        if self.cov_html:
            self.bazel.run(
                "//tools/testing:python_coverage",
                self.cov_path, self.cov_html)
        return super().on_checks_complete()


def main(*args: list) -> None:
    return PytestChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
