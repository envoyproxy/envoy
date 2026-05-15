import re
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, TextIO, Tuple

import yaml

AREA_RE = re.compile(r"^[a-z][a-z0-9_]*$")
DEFAULT_SLUG_MAX_LENGTH = 39


class UsageError(Exception):
    pass


def ensure_tty(stdin: TextIO, missing_args: Iterable[str]) -> None:
    missing = [arg for arg in missing_args if arg]
    if missing and not is_tty(stdin):
        raise UsageError(
            f"Missing required argument(s) {', '.join(missing)} and stdin is not a TTY.")


def is_tty(stream: Optional[TextIO]) -> bool:
    return bool(stream and getattr(stream, "isatty", lambda: False)())


def prompt_with_default(prompt: str, default: Optional[str] = None) -> str:
    suffix = f" [{default}]" if default else ""
    value = input(f"{prompt}{suffix}: ").strip()
    return value or (default or "")


def prompt_choice(prompt: str, choices: Iterable[str]) -> str:
    options = list(choices)
    while True:
        print(f"Valid {prompt} values: {', '.join(options)}")
        value = input(f"{prompt}: ").strip()
        if value in options:
            return value
        print(f"Invalid {prompt}: {value}")


def prompt_multiline(prompt: str) -> str:
    print(f"{prompt} (finish with EOF / Ctrl-D):")
    lines: List[str] = []
    while True:
        try:
            lines.append(input())
        except EOFError:
            break
    return "\n".join(lines)


def repo_root(environ: Mapping[str, str]) -> Path:
    return Path(environ.get("BUILD_WORKSPACE_DIRECTORY", Path.cwd())).resolve()


def changelogs_root(root: Path) -> Path:
    return root / "changelogs"


def sections_path(root: Path) -> Path:
    return changelogs_root(root) / "sections.yaml"


def areas_path(root: Path) -> Path:
    return changelogs_root(root) / "areas.yaml"


def current_root(root: Path) -> Path:
    return changelogs_root(root) / "current"


def load_yaml_mapping(path: Path, missing_hint: str) -> Dict[str, Dict[str, str]]:
    if not path.exists():
        raise UsageError(f"Required file {path} is missing. {missing_hint}")
    data = yaml.safe_load(path.read_text()) or {}
    if not isinstance(data, dict):
        raise UsageError(f"Expected {path} to contain a YAML mapping.")
    return data


def load_sections(root: Path) -> Dict[str, Dict[str, str]]:
    return load_yaml_mapping(
        sections_path(root),
        "Ensure the changelog files are present in your checkout.",
    )


def load_areas(root: Path) -> Dict[str, Dict[str, str]]:
    return load_yaml_mapping(
        areas_path(root),
        "Run this helper on a branch that includes the per-entry changelog migration (#45093).",
    )


def validate_section(section: str, valid_sections: Iterable[str]) -> None:
    if section not in set(valid_sections):
        raise UsageError(f"Unknown changelog section '{section}'.")


def validate_area(area: str, valid_areas: Iterable[str]) -> None:
    if area not in set(valid_areas):
        raise UsageError(
            f"Unknown changelog area '{area}'. Run ./ci/do_ci.sh changelog-area-add first.")


def validate_area_name(area: str) -> None:
    if not AREA_RE.match(area):
        raise UsageError(
            f"Invalid area '{area}'. Area names must match {AREA_RE.pattern}.")


def normalize_change(change: str) -> str:
    normalized = change.rstrip()
    if not normalized.strip():
        raise UsageError("Changelog text must not be empty.")
    return f"{normalized}\n"


def slugify(text: str, max_length: int = DEFAULT_SLUG_MAX_LENGTH) -> str:
    first_line = text.strip().splitlines()[0] if text.strip() else ""
    words = re.findall(r"[a-z0-9]+", first_line.lower())
    if not words:
        return "change"
    slug_parts: List[str] = []
    slug = ""
    for word in words:
        candidate = f"{slug}-{word}" if slug else word
        if slug and len(candidate) > max_length:
            break
        slug_parts.append(word)
        slug = "-".join(slug_parts)
    return slug or words[0][:max_length]


def next_entry_path(root: Path, section: str, area: str, change: str) -> Path:
    slug = slugify(change)
    section_dir = current_root(root) / section
    section_dir.mkdir(parents=True, exist_ok=True)
    base = section_dir / f"{area}__{slug}"
    candidate = base.with_suffix(".rst")
    index = 2
    while candidate.exists():
        candidate = Path(f"{base}-{index}.rst")
        index += 1
    return candidate


def split_leading_comment_block(text: str) -> Tuple[str, str]:
    lines = text.splitlines(keepends=True)
    index = 0
    while index < len(lines) and (lines[index].startswith("#") or lines[index].strip() == ""):
        index += 1
    return "".join(lines[:index]), "".join(lines[index:])


def dump_areas(areas: Mapping[str, Mapping[str, str]]) -> str:
    ordered = {name: {"title": data["title"]} for name, data in sorted(areas.items())}
    return yaml.safe_dump(ordered, sort_keys=False)
