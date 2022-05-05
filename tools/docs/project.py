import pathlib
from functools import cached_property
from typing import Dict, List, Optional, Tuple, TypedDict

from frozendict import frozendict
from packaging import version
import yaml

VERSION_HISTORY_SECTIONS: frozendict = frozendict(
    changes=frozendict(title="Changes"),
    behavior_changes=frozendict(
        title="Incompatible behavior changes",
        description=
        "*Changes that are expected to cause an incompatibility if applicable; deployment changes are likely required*"
    ),
    minor_behavior_changes=frozendict(
        title="Minor behavior changes",
        description=
        "*Changes that may cause incompatibilities for some users, but should not for most*"),
    bug_fixes=frozendict(
        title="Bug fixes",
        description=
        "*Changes expected to improve the state of the world and are unlikely to have negative effects*"
    ),
    removed_config_or_runtime=frozendict(
        title="Removed config or runtime",
        description="*Normally occurs at the end of the* :ref:`deprecation period <deprecated>`"),
    new_features=frozendict(title="New features"),
    deprecated=frozendict(title="Deprecated"))


def minor_version_for(_version: version.Version) -> version.Version:
    return version.Version(f"{_version.major}.{_version.minor}")


class ChangelogError(Exception):
    pass


class ChangeDict(TypedDict):
    area: str
    change: str


ChangeList = List[ChangeDict]


class BaseChangelogDict(TypedDict):
    date: str


class ChangelogDict(BaseChangelogDict, total=False):
    changes: ChangeList
    behavior_changes: ChangeList
    minor_behavior_changes: ChangeList
    bug_fixes: ChangeList
    removed_config_or_runtime: ChangeList
    new_features: ChangeList
    deprecated: ChangeList


class ChangelogEntry:

    def __init__(self, section, entry):
        self.section = section
        self.entry = entry

    def __gt__(self, other):
        if self.area > other.area:
            return True
        if self.change > other.change:
            return True
        return False

    def __lt__(self, other):
        return not self.__gt__(other)

    @property
    def area(self):
        return self.entry["area"]

    @property
    def change(self):
        return self.entry["change"]


class Changelog:

    def __init__(self, version: version.Version, path: pathlib.Path):
        self._version = version
        self.path = path

    @cached_property
    def data(self) -> ChangelogDict:
        try:
            return yaml.safe_load(self.path.read_text())
        except yaml.reader.ReaderError as e:
            raise ChangelogError(f"Failed to parse changelog ({self.path}): {e}")

    @property
    def release_date(self) -> str:
        return self.data["date"]

    @property
    def version(self):
        return f"{self._version.base_version}"

    def entries(self, section):
        return sorted(ChangelogEntry(section, entry) for entry in self.data[section])


class Project:

    def __init__(self, version_path: pathlib.Path, changelogs: Tuple[pathlib.Path, ...]):
        self.version_path = version_path
        self._changelogs = changelogs

    @cached_property
    def archived_versions(self) -> Tuple[version.Version, ...]:
        """Non/archived version logic."""
        non_archive = (5 if self.is_main_dev else 4)
        return tuple(reversed(sorted(self.minor_versions.keys())))[non_archive:]

    @cached_property
    def changelogs(self) -> Dict[version.Version, Changelog]:
        changelog_versions = {
            version.Version(changelog.stem): changelog
            for changelog in self._changelogs
        }
        return {
            k: Changelog(k, changelog_versions[k])
            for k in reversed(sorted(changelog_versions.keys()))
        }

    @property
    def current_changelog(self) -> Changelog:
        return next(iter(self.changelogs))

    @cached_property
    def current_minor_version(self) -> version.Version:
        return minor_version_for(self.current_version)

    @cached_property
    def current_version(self) -> version.Version:
        return version.Version(self.version_path.read_text())

    @cached_property
    def dev_version(self) -> Optional[version.Version]:
        return list(self.changelogs)[0] if self.is_dev else None

    @property
    def is_dev(self) -> bool:
        return self.current_version.is_devrelease

    @property
    def is_main_dev(self) -> bool:
        """If the patch version is `0` and its a dev branch then we are on `main`"""
        return self.is_dev and self.current_version.micro == 0

    @cached_property
    def minor_versions(self) -> Dict[version.Version, Tuple[version.Version, ...]]:
        minor_versions = {}
        for changelog_version in self.changelogs:
            minor_version = minor_version_for(changelog_version)
            minor_versions[minor_version] = minor_versions.get(minor_version, [])
            minor_versions[minor_version].append(changelog_version)
        return {k: self._patch_versions(v) for k, v in minor_versions.items()}

    @cached_property
    def stable_versions(self) -> Tuple[version.Version, ...]:
        exclude = set(self.archived_versions)
        if self.is_main_dev:
            exclude.add(list(self.minor_versions.keys())[0])
        return tuple(reversed(sorted(set(self.minor_versions.keys()) - exclude)))

    def is_current(self, version):
        return self.current_version.base_version == version.base_version

    def _patch_versions(self, versions):
        return tuple(
            reversed(
                sorted(
                    versions if not self.is_dev else (
                        v for v in versions if not self.is_current(v)))))
