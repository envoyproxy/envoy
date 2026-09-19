#!/usr/bin/env python3
"""Helpers for scanning and rewriting bzlmod registry pins in MODULE.bazel files."""

import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Iterable

_CALL_RE = re.compile(
    r"(?P<kind>bazel_dep|single_version_override)\((?P<body>.*?)\)", re.DOTALL
)
_ATTR_RE_TEMPLATE = r'\b%s\s*=\s*"(?P<value>[^"]*)"'


@dataclass(frozen=True)
class ModulePinOccurrence:
    module_name: str
    version: str
    file_path: str
    version_span: tuple[int, int]


class ModulePinError(ValueError):
    """Raised when module pins are malformed or inconsistent."""



def _attribute_match(body: str, attribute: str) -> re.Match[str] | None:
    return re.search(_ATTR_RE_TEMPLATE % re.escape(attribute), body)



def iter_module_pins(file_path: str, content: str) -> Iterable[ModulePinOccurrence]:
    for call_match in _CALL_RE.finditer(content):
        kind = call_match.group("kind")
        body = call_match.group("body")
        name_attribute = "name" if kind == "bazel_dep" else "module_name"
        name_match = _attribute_match(body, name_attribute)
        if name_match is None:
            continue
        version_match = _attribute_match(body, "version")
        if version_match is None:
            continue
        version_start = call_match.start("body") + version_match.start("value")
        version_end = call_match.start("body") + version_match.end("value")
        yield ModulePinOccurrence(
            module_name=name_match.group("value"),
            version=version_match.group("value"),
            file_path=file_path,
            version_span=(version_start, version_end),
        )



def scan_module_files(file_paths: Iterable[str]) -> list[dict[str, object]]:
    modules: dict[str, dict[str, object]] = {}
    for file_path in file_paths:
        content = pathlib.Path(file_path).read_text()
        for occurrence in iter_module_pins(file_path, content):
            module_entry = modules.setdefault(
                occurrence.module_name,
                {
                    "name": occurrence.module_name,
                    "files": [],
                    "versions": [],
                },
            )
            files = module_entry["files"]
            if occurrence.file_path not in files:
                files.append(occurrence.file_path)
            versions = module_entry["versions"]
            if occurrence.version not in versions:
                versions.append(occurrence.version)
    return sorted(modules.values(), key=lambda module: str(module["name"]))



def rewrite_module_files(
    file_paths: Iterable[str], replacements: dict[str, str]
) -> list[dict[str, object]]:
    changed_modules: dict[str, dict[str, object]] = {}
    for file_path in file_paths:
        path = pathlib.Path(file_path)
        content = path.read_text()
        occurrences = list(iter_module_pins(file_path, content))
        replacements_in_file: list[tuple[int, int, str]] = []
        for occurrence in occurrences:
            new_version = replacements.get(occurrence.module_name)
            if new_version is None or new_version == occurrence.version:
                continue
            changed = changed_modules.setdefault(
                occurrence.module_name,
                {
                    "name": occurrence.module_name,
                    "from_versions": [],
                    "to": new_version,
                    "files": [],
                },
            )
            if changed["to"] != new_version:
                raise ModulePinError(
                    "Conflicting replacements for module %s: %s != %s"
                    % (occurrence.module_name, changed["to"], new_version)
                )
            if occurrence.version not in changed["from_versions"]:
                changed["from_versions"].append(occurrence.version)
            if file_path not in changed["files"]:
                changed["files"].append(file_path)
            start, end = occurrence.version_span
            replacements_in_file.append((start, end, new_version))
        if not replacements_in_file:
            continue
        for start, end, new_version in reversed(replacements_in_file):
            content = content[:start] + new_version + content[end:]
        path.write_text(content)
    return sorted(
        [
            {
                **module,
                "from": ", ".join(module["from_versions"]),
            }
            for module in changed_modules.values()
        ],
        key=lambda module: str(module["name"]),
    )



def _parse_replacements(entries: Iterable[str]) -> dict[str, str]:
    replacements: dict[str, str] = {}
    for entry in entries:
        if "=" not in entry:
            raise ModulePinError("Invalid replacement %r, expected name=version" % entry)
        name, version = entry.split("=", 1)
        if not name or not version:
            raise ModulePinError("Invalid replacement %r, expected name=version" % entry)
        replacements[name] = version
    return replacements



def _scan_command(args: argparse.Namespace) -> int:
    json.dump({"modules": scan_module_files(args.module_files)}, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0



def _rewrite_command(args: argparse.Namespace) -> int:
    changes = rewrite_module_files(args.module_files, _parse_replacements(args.replace))
    json.dump({"modules": changes}, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0



def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    scan_parser = subparsers.add_parser("scan")
    scan_parser.add_argument("module_files", nargs="+")
    scan_parser.set_defaults(func=_scan_command)

    rewrite_parser = subparsers.add_parser("rewrite")
    rewrite_parser.add_argument("module_files", nargs="+")
    rewrite_parser.add_argument("--replace", action="append", default=[])
    rewrite_parser.set_defaults(func=_rewrite_command)

    return parser.parse_args(argv)



def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    try:
        return args.func(args)
    except ModulePinError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
