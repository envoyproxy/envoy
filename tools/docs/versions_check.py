import pathlib
import re
import sys
from functools import cached_property
from typing import Iterator, List, Pattern

from packaging import version

from aio.run import checker

from envoy.base.utils import IProject, Project

INVALID_REFLINK = r"[^:]ref:`"
REF_WITH_PUNCTUATION_REGEX = r".*\. <[^<]*>`\s*"

# Make sure backticks come in pairs.
# Exceptions: reflinks (ref:`` where the backtick won't be preceded by a space
#             links `title <link>`_ where the _ is checked for in the regex.
SINGLE_TICK_REGEX = re.compile(r"[^`]`[^`].*`[^`]")
REF_TICKS_REGEX = re.compile(r":`[^`]*`")
LINK_TICKS_REGEX = re.compile(r"[^`]`[^`].*`_")


class VersionFile:

    def __init__(self, project, changelog):
        self.project = project
        self.changelog = changelog

    @cached_property
    def invalid_reflink_re(self) -> Pattern[str]:
        return re.compile(INVALID_REFLINK)

    @cached_property
    def link_ticks_re(self) -> Pattern[str]:
        return re.compile(LINK_TICKS_REGEX)

    @cached_property
    def punctuation_re(self) -> Pattern[str]:
        return re.compile(REF_WITH_PUNCTUATION_REGEX)

    @cached_property
    def ref_ticks_re(self) -> Pattern[str]:
        return re.compile(REF_TICKS_REGEX)

    @cached_property
    def single_tick_re(self) -> Pattern[str]:
        return re.compile(SINGLE_TICK_REGEX)

    def check_punctuation(self, section, entry) -> List[str]:
        change = entry["change"]
        if change.strip().endswith("."):
            return []
        # Ends with punctuated link
        if change.strip().endswith('`') and self.punctuation_re.match(change.strip()):
            return []
        # Ends with a list
        if change.strip().split("\n")[-1].startswith("  *"):
            return []
        return [
            f"{self.changelog.version}: Missing punctuation ({section}/{entry['area']}) ...{change[-30:]}\n{entry['change']}"
        ]

    def check_reflinks(self, section, entry) -> List[str]:
        return ([
            f"{self.changelog.version}: Found text \" ref:\" ({section}/{entry['area']})  This should probably be \" :ref:\"\n{entry['change']}"
        ] if self.invalid_reflink_re.findall(entry["change"]) else [])

    def check_change(self, section, entry):
        return [
            *self.check_reflinks(section, entry), *self.check_ticks(section, entry),
            *self.check_punctuation(section, entry)
        ]

    def check_ticks(self, section, entry) -> List[str]:
        _change = entry["change"]
        for reflink in self.ref_ticks_re.findall(_change):
            _change = _change.replace(reflink, "")
        for extlink in self.link_ticks_re.findall(_change):
            _change = _change.replace(extlink, "")
        single_ticks = self.single_tick_re.findall(_change)
        return ([
            f"{self.changelog.version}: Single backticks found ({section}/{entry['area']}) {', '.join(single_ticks)}\n{_change}"
        ] if single_ticks else [])

    def run_checks(self) -> Iterator[str]:
        errors = []
        for section, entries in self.changelog.data.items():
            if section == "date":
                continue
            if section not in self.project.changelogs.sections:
                errors.append(f"{self.changelog.version} Unrecognized changelog section: {section}")
            if section == "changes":
                if version.Version(self.changelog.version) > version.Version("1.16"):
                    errors.append(f"Removed `changes` section found: {self.changelog.version}")
            if not entries:
                continue
            for entry in entries:
                errors.extend(self.check_change(section, entry))
        return errors


class VersionsChecker(checker.Checker):
    checks = ("changelogs", "pending", "version")

    @cached_property
    def project(self) -> IProject:
        return Project(self.args.version)

    def add_arguments(self, parser):
        parser.add_argument("version")
        super().add_arguments(parser)

    async def check_changelogs(self):
        for changelog in self.project.changelogs.values():
            errors = VersionFile(self.project, changelog).run_checks()
            if errors:
                self.error("changelogs", errors)
            else:
                self.succeed("changelogs", [f"{changelog.version}"])

    async def check_pending(self):
        pending = [
            k.base_version
            for k, v in self.project.changelogs.items()
            if v.release_date == "Pending"
        ]
        all_good = (
            not pending or (self.project.is_dev and pending == [self.project.version.base_version]))
        if all_good:
            self.succeed("pending", [f"No extraneous pending versions found"])
            return
        pending = [x for x in pending if x != self.project.version.base_version]
        if self.project.is_dev:
            self.error("pending", [f"Only current version should be pending, found: {pending}"])
        else:
            self.error("pending", [f"Nothing should be pending, found: {pending}"])

    async def check_version(self) -> None:
        current_is_duplicated = (
            pathlib.Path(f"changelogs/{self.project.version.base_version}.yaml")
            in self.project.changelogs.paths)
        if current_is_duplicated:
            self.error(
                "version", [
                    f"A changelog file is present matching the version in VERSION.txt ({self.project.version.base_version}). "
                    "The current version should be specified in `current.yaml`. "
                    "Increase the version or remove the spurious changelog file."
                ])
        else:
            self.succeed("version", ["Only `current.yaml` changelog found for current version"])
        if self.project.is_current(self.project.changelogs.current):
            self.succeed("version", ["VERSION.txt version matches most recent changelog"])
        else:
            self.error("version", ["VERSION.txt does not match most recent changelog"])
        not_pending = (
            self.project.is_dev and
            not self.project.changelogs[self.project.changelogs.current].release_date == "Pending")
        not_dev = (
            not self.project.is_dev
            and self.project.changelogs[self.project.changelogs.current].release_date == "Pending")
        if not_pending:
            self.error(
                "version",
                ["VERSION.txt is set to `-dev` but most recent changelog is not `Pending`"])
        elif not_dev:
            self.error(
                "version",
                ["VERSION.txt is not set to `-dev` but most recent changelog is `Pending`"])
        elif self.project.is_dev:
            self.succeed(
                "version", ["VERSION.txt is set to `-dev` and most recent changelog is `Pending`"])
        else:
            self.succeed(
                "version",
                ["VERSION.txt is not set to `-dev` and most recent changelog is not `Pending`"])


def main(*args: str) -> int:
    return VersionsChecker(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
