#!/usr/bin/python3

import os
import sys
from functools import cached_property

import yaml

DEPENDABOT_CONFIG = ".github/dependabot.yml"

# TODO(phlax): add checks for:
#      - requirements can be installed together
#      - pip-compile formatting (maybe do that in code_format)


# TODO(phlax): move this to a base module
class Checker(object):
    """Runs check methods prefixed with `check_` and named in `self.checks`

    check methods should return the count of errors and handle writing to stderr
    """
    checks = ()

    def __init__(self, path: str):
        self.path = path

    def error_lines(self, errors: list) -> str:
        """Transform a list of errors to a string for output"""
        return "\n - ".join([""] + errors)

    def run_checks(self) -> int:
        """Run all configured checks and return the sum of their error counts"""
        return sum(getattr(self, f"check_{check}")() for check in self.checks)

    def write_errors(self, errors: list, pre: str, post: str) -> None:
        """Write errors to stderr with pre/post ambles"""
        sys.stderr.write(f"\n{pre}: \n{self.error_lines(errors)}\n\n{post}\n\n")


class PipChecker(Checker):
    checks = ("dependabot",)

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
        with open(os.path.join(self.path, DEPENDABOT_CONFIG)) as f:
            return yaml.safe_load(f.read())

    @cached_property
    def requirements_dirs(self) -> set:
        """Set of found directories in the repo containing requirements.txt"""
        return set(
            root[len(self.path):]
            for root, dirs, files in os.walk(self.path)
            if "requirements.txt" in files)

    def check_dependabot(self) -> int:
        """Check that dependabot config matches requirements.txt files found in repo"""
        missing_dirs = self.config_requirements - self.requirements_dirs
        missing_config = self.requirements_dirs - self.config_requirements

        if missing_config:
            self.write_errors(
                sorted(missing_config), "Missing requirements config for",
                "Either add the missing config or remove the file/s")

        if missing_dirs:
            self.write_errors(
                sorted(missing_dirs), "Missing requirements files for",
                "Either add the missing file/s or remove the config")

        return len(missing_config | missing_dirs)


def main(path: str) -> None:
    errors = PipChecker(path).run_checks()
    if errors:
        raise SystemExit(f"Pip checks failed: {errors} errors")


if __name__ == "__main__":
    try:
        path = sys.argv[1]
    except IndexError:
        raise SystemExit("Pip check tool must be called with a path argument")
    main(path)
