#!/usr/bin/env python3

# Runs pytest
#
# Usage:
#
#   bazel run //tools/testing:python_pytest -- -h
#
# or (with pytest installed):
#
#   ./tools/testing/python_pytest.py -h
#

import argparse
import sys

import pytest

from aio.run import runner

from envoy.base import utils


class PytestRunner(runner.Runner):
    _use_uvloop = False

    @property
    def cov_collect(self) -> str:
        """The path to a file to collect coverage in"""
        return self.args.cov_collect

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        """Add arguments to the arg parser"""
        super().add_arguments(parser)
        parser.add_argument("--cov-collect", default=None, help="Collect coverage data to path")

    def pytest_args(self, coveragerc: str) -> list:
        return self.extra_args + [f"--cov-config={coveragerc}"]

    async def run(self) -> int:
        if not self.cov_collect:
            return pytest.main(self.extra_args)

        with utils.coverage_with_data_file(self.cov_collect) as coveragerc:
            return pytest.main(self.pytest_args(coveragerc))


def main(*args) -> int:
    return PytestRunner(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
