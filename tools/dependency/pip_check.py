#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  $ bazel run //tools/dependency:pip_check -- -h
#
#  $ bazel run //tools/dependency:pip_check
#
# with pip:
#
#  $ pip install envoy.dependency.pip_check
#  $ envoy.dependency.pip_check -h
#
# usage with pip requires a path, eg
#
#  $ envoy.dependency.pip_check .
#
# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/pytooling/tree/main/envoy.dependency.pip_check
#
# Please submit issues/PRs to the pytooling repo:
#
#    https://github.com/envoyproxy/pytooling
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
