#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  bazel run //tools/code_format:yaml_check -- -h
#
# alternatively, if you have the necessary python deps available
#
#  ./tools/code_format/yaml_check.py -h
#
# python requires: yamllint
#

import os
import sys
from functools import cached_property

from tools.base import checker, utils


YAMLLINT_CONFIG = ".yamllint"

# explicitly use python3 linter
YAMLLINT_COMMAND = ("python3", "-m", "yamllint")


class YamlChecker(checker.ForkingChecker):
    checks = ("yaml",)

    @property
    def yamllint_command(self) -> str:
        return YAMLLINT_COMMAND + ("-c", self.yamllint_config_path) + (self.path,)

    @property
    def yamllint_config_path(self) -> str:
        return os.path.join(self.path, YAMLLINT_CONFIG)

    def check_yaml(self) -> None:
        """Run yamllint on files and/or repo"""
        result = self.subproc_run(self.yamllint_command)
        if result.returncode:
            print(result.stdout.decode("utf-8"))


def main(*args: list) -> None:
    return YamlChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
