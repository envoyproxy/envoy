#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  $ bazel run //tools/code_format:python_check -- -h
#
#  $ bazel run //tools/code_format:python_check
#
# with pip:
#
#  $ pip install envoy.code_format.python_check
#  $ envoy.code_format.python_check -h
#
# usage with pip requires a path, eg
#
#  $ envoy.code_format.python_check .
#
# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/pytooling/tree/main/envoy.code_format.python_check
#
# Please submit issues/PRs to the pytooling repo:
#
#    https://github.com/envoyproxy/pytooling
#

import pathlib
import sys
from functools import cached_property

import abstracts

from envoy.code_format import python_check

import envoy_repo


@abstracts.implementer(python_check.APythonChecker)
class EnvoyPythonChecker:

    @cached_property
    def path(self) -> pathlib.Path:
        if self.args.paths:
            return pathlib.Path(self.args.paths[0])
        return pathlib.Path(envoy_repo.PATH)


def main(*args) -> int:
    return EnvoyPythonChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
