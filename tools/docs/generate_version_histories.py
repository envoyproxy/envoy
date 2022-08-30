import pathlib
import re
import sys
import tarfile
from functools import cached_property

from frozendict import frozendict
import jinja2
from packaging import version

from aio.run import runner

from envoy.base import utils
from envoy.base.utils import IProject, Project

# TODO(phlax): Move all of this to pytooling

REFLINK_RE = re.compile(r":ref:[^>]*>`")

VERSION_HISTORY_INDEX_TPL = """
.. _version_history:

Version history
---------------

{% if dev_version %}
.. toctree::
  :titlesonly:
  :maxdepth: 2
  :caption: Current development version

  v{{ dev_version.major }}.{{ dev_version.minor }}/v{{ dev_version }}

{% endif %}

Stable versions
===============

{{ stable_message }}

.. toctree::
  :titlesonly:
  :maxdepth: 2
  :caption: Changelog
{% for version in stable_versions %}
  v{{ version.base_version }}: {{ version.release_version }} ({{ version.release_date }}) <v{{ version.base_version }}/v{{ version.base_version }}>
{%- endfor %}

Archived versions
=================

{{ archived_message }}

.. toctree::
  :titlesonly:
  :maxdepth: 1
{% for version in archived_versions %}
  v{{ version.base_version }}: {{ version.release_version }} ({{ version.release_date }}) <v{{ version.base_version }}/v{{ version.base_version }}>
{%- endfor %}

.. _deprecated:

Deprecation Policy
==================

As of release 1.3.0, Envoy will follow a
`Breaking Change Policy <https://github.com/envoyproxy/envoy/blob/main//CONTRIBUTING.md#breaking-change-policy>`_.

Features in the deprecated list for each version have been DEPRECATED
and will be removed in the specified release cycle. A logged warning
is expected for each deprecated item that is in deprecation window.
"""

VERSION_HISTORY_MINOR_INDEX_TPL = """
.. _version_history_{{ minor_version }}:

{{ minor_version }}
{{ "-" * minor_version|length }}

Latest release:
  `{{ current_release }} <https://github.com/envoyproxy/envoy/releases/tag/v{{ current_release }}>`_ ({{ release_date }})
{% if current_release != original_release.version %}
Initial release date:
  {{ original_release_date }}
{% endif %}

.. toctree::
  :titlesonly:
  :maxdepth: 2
  :caption: Changelog
{% for version in patch_versions %}
  v{{ version.base_version }}
{%- endfor %}

"""

VERSION_HISTORY_TPL = """
.. _version_history_{{ changelog.base_version }}:

{{ changelog.base_version }} ({{ release_date }})
{{ "=" * (changelog.base_version|length + release_date|length + 4) }}

{% for name, section in sections.items() %}
{% if data[name] %}
{{ section.title }}
{{ "-" * section.title|length }}
{% if section.description %}
{{ section.description | versionize(mapped_version) }}
{% endif %}

{% for item in entries[name] -%}
* **{{ item.area }}**: {{ item.change | versionize(mapped_version) | indent(width=2, first=false) }}
{%- endfor %}
{% endif %}
{%- endfor %}

"""


def versionize_filter(text, mapped_version):
    """Replace refinks with versioned reflinks."""
    if not mapped_version:
        return text
    version_prefix = f"v{mapped_version.base_version}:"
    matches = set(REFLINK_RE.findall(text))
    replacements = []

    for matched in matches:
        i = matched.find("<") + 1
        remaining = matched[i:]
        if ":" in remaining:
            continue
        replacements.append((matched, f"{matched[:i]}{version_prefix}{matched[i:]}"))

    for sub, replacement in replacements:
        text = text.replace(sub, replacement)
    return text


class VersionHistories(runner.Runner):

    @cached_property
    def jinja_env(self) -> jinja2.Environment:
        env = jinja2.Environment()
        env.filters["versionize"] = versionize_filter
        return env

    @cached_property
    def project(self) -> IProject:
        return Project()

    @cached_property
    def sections(self) -> frozendict:
        return self.project.changelogs.sections

    @cached_property
    def tpath(self) -> pathlib.Path:
        return pathlib.Path(self.tempdir.name)

    @cached_property
    def version_history_index_tpl(self):
        return self.jinja_env.from_string(VERSION_HISTORY_INDEX_TPL)

    @cached_property
    def version_history_minor_index_tpl(self):
        return self.jinja_env.from_string(VERSION_HISTORY_MINOR_INDEX_TPL)

    @cached_property
    def version_history_tpl(self):
        return self.jinja_env.from_string(VERSION_HISTORY_TPL)

    def add_arguments(self, parser) -> None:
        super().add_arguments(parser)
        parser.add_argument("output_file")

    def minor_index_path(self, minor_version) -> pathlib.Path:
        return self.tpath.joinpath(f"v{minor_version.base_version}").joinpath(
            f"v{minor_version.base_version}.rst")

    @runner.cleansup
    async def run(self) -> None:
        await self.write_version_history_index()
        await self.write_version_histories()
        await self.write_version_history_minor_indeces()
        self.write_tarball()

    def write_tarball(self) -> None:
        with tarfile.open(self.args.output_file, "w:gz") as tarball:
            tarball.add(self.tpath, arcname="./")

    async def write_version_histories(self) -> None:
        for changelog_version in self.project.changelogs:
            await self.write_version_history(changelog_version)

    async def write_version_history(self, changelog_version: version.Version) -> None:
        minor_version = utils.minor_version_for(changelog_version)
        root_path = self.tpath.joinpath(f"v{minor_version.base_version}")
        root_path.mkdir(parents=True, exist_ok=True)
        map_version = (
            minor_version < self.project.minor_version
            and (minor_version in self.project.inventories.versions))
        changelog = self.project.changelogs[changelog_version]
        data = await changelog.data
        version_history = self.version_history_tpl.render(
            mapped_version=(minor_version if map_version else None),
            sections=self.sections,
            data=data,
            entries={
                s: sorted(await changelog.entries(s), key=self._sort_entries)
                for s in self.sections
                if s in data
            },
            release_date=await changelog.release_date,
            changelog=changelog)
        version_path = root_path.joinpath(f"v{changelog_version}.rst")
        version_path.write_text(f"{version_history.strip()}\n\n")

    async def write_version_history_index(self) -> None:
        stable_message = (
            "Versions that are currently supported." if self.project.is_main_dev else
            "Versions that were supported when this branch was initially released.")
        archived_message = (
            "Versions that are no longer supported." if self.project.is_main_dev else
            "Versions that were no longer supported when this branch was initially released.")

        class _Version:

            def __init__(self, version):
                self.version = version
                self.base_version = version.base_version

        stable_versions = [_Version(v) for v in self.project.stable_versions]
        for v in stable_versions:
            release_version = self.project.changelogs[self.project.minor_versions[v.version][0]]
            v.release_version = release_version.version
            v.release_date = await release_version.release_date

        archived_versions = [_Version(v) for v in self.project.archived_versions]
        for v in archived_versions:
            release_version = self.project.changelogs[self.project.minor_versions[v.version][0]]
            v.release_version = release_version.version
            v.release_date = await release_version.release_date

        version_history_rst = self.version_history_index_tpl.render(
            archived_message=archived_message,
            stable_message=stable_message,
            dev_version=self.project.dev_version,
            changelogs=self.project.changelogs,
            minor_versions=self.project.minor_versions,
            stable_versions=stable_versions,
            archived_versions=archived_versions)
        self.tpath.joinpath("version_history.rst").write_text(f"{version_history_rst.strip()}\n\n")

    async def write_version_history_minor_indeces(self) -> None:
        for i, (minor_version, patches) in enumerate(self.project.minor_versions.items()):
            if self.project.is_main_dev and i == 0:
                continue
            await self.write_version_history_minor_index(minor_version, patches)

    async def write_version_history_minor_index(
            self, minor_version: version.Version, patch_versions) -> None:
        skip_first = (self.project.is_dev and self.project.is_current(patch_versions[0]))
        if skip_first:
            patch_versions = patch_versions[1:]
        current_release = patch_versions[0]
        original_release = self.project.changelogs[patch_versions[-1]]
        version_history_minor_index = self.version_history_minor_index_tpl.render(
            minor_version=f"v{minor_version.base_version}",
            current_release=current_release.base_version,
            original_release=original_release,
            original_release_date=await original_release.release_date,
            release_date=await self.project.changelogs[current_release].release_date,
            patch_versions=patch_versions)
        self.minor_index_path(minor_version).write_text(
            f"{version_history_minor_index.strip()}\n\n")

    def _sort_entries(self, entry):
        # TODO(phlax): fix sorting in `envoy.base.utils` and remove
        return entry.area, entry.change


def main(*args):
    return VersionHistories(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
