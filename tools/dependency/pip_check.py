#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  bazel //tools/dependency:pip_check -- -h
#
# alternatively, if you have the necessary python deps available
#
#  ./tools/dependency/pip_check.py -h
#

import pathlib
import sys
from functools import cached_property

import abstracts

from envoy.dependency import pip_check

import envoy_repo


@abstracts.implementer(pip_check.APipChecker)
class EnvoyPipChecker:

    @cached_property
    def path(self) -> pathlib.Path:
        if self.args.paths:
            return pathlib.Path(self.args.paths[0])
        return pathlib.Path(envoy_repo.PATH)


def main(*args) -> int:
    return EnvoyPipChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
