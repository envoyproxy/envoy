#!/usr/bin/env python3

#
# Takes a coverage file and generates html
#
# Usage:
#
#   bazel run //tools/testing:python_coverage -- -h
#
# or (with coverage installed):
#
#   ./tools/testing/python_coverage.py -h
#

import argparse
import sys

from coverage import cmdline  # type:ignore

from tools.base import runner, utils


class CoverageRunner(runner.Runner):

    @property
    def cov_data(self):
        """The path to a file to a file containing coverage data"""
        return self.args.cov_data

    @property
    def cov_html(self):
        """The path to a file to a file containing coverage data"""
        return self.args.cov_html

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """Add arguments to the arg parser"""
        parser.add_argument("cov_data", help="Path to coverage data")
        parser.add_argument("cov_html", help="Path to coverage html")

    def coverage_args(self, coveragerc: str) -> list:
        return ["html"] + self.extra_args + [f"--rcfile={coveragerc}", "-d", self.cov_html]

    def run(self) -> int:
        if not self.cov_data:
            return cmdline.main(self.extra_args)

        with utils.coverage_with_data_file(self.cov_data) as coveragerc:
            return cmdline.main(self.coverage_args(coveragerc))


def main(*args) -> int:
    return CoverageRunner(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
