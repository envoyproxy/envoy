#!/usr/bin/env python3

import argparse
import pathlib
import re
from collections.abc import Iterable

import yaml


MAX_SLUG_LENGTH = 40
SLUG_SOURCE_LENGTH = 60
ENTRY_SEPARATOR = "__"

CHANGELOGS_CONFIG_HEADER = (
    "# NB: this file is the canonical changelog config consumed by\n"
    "#     envoy.base.utils / envoy.code.check.\n"
    "#\n"
    "# `sections:` lists the valid top-level changelog sections (e.g.\n"
    "#     `bug_fixes`, `new_features`). Adding/removing a section requires\n"
    "#     coordinated updates in toolshed.\n"
    "#\n"
    "# `areas:` lists the canonical set of areas accepted by per-entry\n"
    "#     changelog filenames in changelogs/current/<section>/<area>__<slug>.rst.\n"
    "#     In filenames, '/' MUST be encoded as '~'.\n"
    "#     Adding a new area requires a PR updating this file.\n"
)


def _truncate_slug(slug: str, max_length: int = MAX_SLUG_LENGTH) -> str:
    if len(slug) <= max_length:
        return slug
    truncated = slug[:max_length].rstrip("-")
    if "-" in truncated:
        candidate = truncated.rsplit("-", 1)[0].rstrip("-")
        if candidate:
            return candidate
    return truncated


def _base_slug(change: str) -> str:
    text = change.strip()
    if not text:
        return "entry"
    first_sentence = text.split(". ", 1)[0]
    slug_source = (
        first_sentence
        if len(first_sentence) <= SLUG_SOURCE_LENGTH
        else first_sentence[:SLUG_SOURCE_LENGTH]
    )
    normalized = re.sub(r"[^a-z0-9]+", "-", slug_source.lower()).strip("-")
    if not normalized:
        normalized = "entry"
    return _truncate_slug(normalized)


def _encode_area(area: str) -> str:
    return area.replace("/", "~")


def _write_entry_file(path: pathlib.Path, change: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(change.rstrip("\n") + "\n")


def _render_changelogs_config(
        sections_data: dict,
        areas: Iterable[str]) -> str:
    config = {
        "sections": sections_data or {},
        "areas": {area: {"title": area} for area in sorted(set(areas))},
    }
    return (
        f"{CHANGELOGS_CONFIG_HEADER}\n"
        f"{yaml.safe_dump(config, sort_keys=False, default_flow_style=False)}")


def migrate(project_root: pathlib.Path) -> None:
    changelogs_dir = project_root / "changelogs"
    current_yaml_path = changelogs_dir / "current.yaml"
    sections_yaml_path = changelogs_dir / "sections.yaml"
    current_entries_dir = changelogs_dir / "current"
    changelogs_config_path = changelogs_dir / "changelogs.yaml"

    if not current_yaml_path.exists():
        raise FileNotFoundError(f"Missing required changelog file: {current_yaml_path}")
    if not sections_yaml_path.exists():
        raise FileNotFoundError(f"Missing required changelog file: {sections_yaml_path}")

    current_data = yaml.safe_load(current_yaml_path.read_text()) or {}
    sections_data = yaml.safe_load(sections_yaml_path.read_text()) or {}

    date = current_data.get("date", "Pending")
    slug_counts: dict[tuple[str, str, str], int] = {}
    areas: list[str] = []

    for section, section_entries in current_data.items():
        if section == "date":
            continue
        if section not in sections_data:
            raise ValueError(f"Unknown section '{section}' found in {current_yaml_path}")
        if not section_entries:
            continue
        for entry in section_entries:
            area = entry.get("area", "")
            change = entry.get("change", "")
            if not area:
                raise ValueError(f"Entry in section '{section}' is missing a non-empty 'area' value")
            if not change or not change.strip():
                raise ValueError(
                    f"Entry in section '{section}' and area '{area}' is missing a non-empty 'change' value")
            if area not in areas:
                areas.append(area)
            area_encoded = _encode_area(area)
            slug = _base_slug(change)
            slug_key = (section, area_encoded, slug)
            slug_counts[slug_key] = slug_counts.get(slug_key, 0) + 1
            count = slug_counts[slug_key]
            if count > 1:
                slug = f"{slug}-{count}"
            entry_path = current_entries_dir / section / f"{area_encoded}{ENTRY_SEPARATOR}{slug}.rst"
            _write_entry_file(entry_path, change)

    current_yaml_path.write_text(yaml.safe_dump({"date": date}, sort_keys=False))
    changelogs_config_path.write_text(_render_changelogs_config(sections_data, areas))
    # `sections.yaml` is superseded by `changelogs.yaml` (which now contains
    # both `sections:` and `areas:`).
    sections_yaml_path.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Migrate changelogs/current.yaml to changelogs/current/<section>/<area>__<slug>.rst"
    )
    parser.add_argument(
        "--root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[2],
        help="Envoy repository root path",
    )
    args = parser.parse_args()
    migrate(args.root)


if __name__ == "__main__":
    main()
