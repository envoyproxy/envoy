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

import os
import sys
from functools import cached_property

from tools.base import checker, utils

DEPENDABOT_CONFIG = ".github/dependabot.yml"
REQUIREMENTS_FILENAME = "requirements.txt"

# TODO(phlax): add checks for:
#      - requirements can be installed together
#      - pip-compile formatting


class PipChecker(checker.Checker):
    checks = ("dependabot",)
    _dependabot_config = DEPENDABOT_CONFIG
    _requirements_filename = REQUIREMENTS_FILENAME

    @cached_property
    def config_requirements(self) -> set:
        """Set of configured pip dependabot directories"""
        return set(
            update['directory']
            for update in self.dependabot_config["updates"]
            if update["package-ecosystem"] == "pip")

    @cached_property
    def dependabot_config(self) -> dict:
        """Parsed dependabot config"""
        return utils.from_yaml(os.path.join(self.path, self.dependabot_config_path))

    @property
    def dependabot_config_path(self) -> str:
        return self._dependabot_config

    @cached_property
    def requirements_dirs(self) -> set:
        """Set of found directories in the repo containing requirements.txt"""
        return set(
            root[len(self.path):]
            for root, dirs, files in os.walk(self.path)
            if self.requirements_filename in files)

    @property
    def requirements_filename(self) -> str:
        return self._requirements_filename

    def check_dependabot(self) -> None:
        """Check that dependabot config matches requirements.txt files found in repo"""
        missing_dirs = self.config_requirements.difference(self.requirements_dirs)
        missing_config = self.requirements_dirs.difference(self.config_requirements)
        correct = self.requirements_dirs.intersection(self.config_requirements)
        if correct:
            self.dependabot_success(correct)
        if missing_dirs:
            self.dependabot_errors(
                missing_dirs,
                f"Missing {self.requirements_filename} dir, specified in dependabot config")
        if missing_config:
            self.dependabot_errors(
                missing_config,
                f"Missing dependabot config for {self.requirements_filename} in dir")

    def dependabot_success(self, correct: list) -> None:
        self.succeed(
            "dependabot",
            ([f"{self.requirements_filename}: {dirname}" for dirname in sorted(correct)]))

    def dependabot_errors(self, missing: list, msg: str) -> None:
        for dirname in sorted(missing):
            self.error("dependabot", [f"{msg}: {dirname}"])


def main(*args) -> int:
    return PipChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
