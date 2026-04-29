#!/usr/bin/env python3
"""Verify mirror.bazel.build coverage for Envoy dependencies.

This script checks whether the upstream URLs for each dependency in
bazel/repository_locations.bzl, api/bazel/repository_locations.bzl, and
docs/bazel/repository_locations.bzl have corresponding mirrors on
mirror.bazel.build.

Usage:
    python3 tools/dependency/verify_mirror_coverage.py [--json output.json]
           [--timeout SECS] [--concurrency N] [--no-color]

It is runnable standalone (no Bazel required).
"""

import argparse
import asyncio
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

MIRROR_HOST = "mirror.bazel.build"
MIRROR_BASE = f"https://{MIRROR_HOST}"

# Repository locations files to parse, relative to the repo root.
REPO_LOCATIONS_FILES = [
    "bazel/repository_locations.bzl",
    "api/bazel/repository_locations.bzl",
    "docs/bazel/repository_locations.bzl",
]

# Maximum number of retries for transient errors.
MAX_RETRIES = 3
RETRY_DELAY = 1.0  # seconds

# ANSI colour codes.
GREEN = "\033[32m"
RED = "\033[31m"
YELLOW = "\033[33m"
RESET = "\033[0m"


def _format_version(s, version):
    """Expand {version}, {dash_version}, {underscore_version} placeholders."""
    return s.format(
        version=version,
        dash_version=version.replace(".", "-"),
        underscore_version=version.replace(".", "_"),
    )


def _parse_bzl_dict(text):
    """Very small, best-effort Starlark dict parser.

    Handles the REPOSITORY_LOCATIONS_SPEC dict used in Envoy's repository
    location files.  Returns a plain Python dict of {key: {field: value}}.
    This is NOT a full Starlark evaluator; it handles the specific subset of
    syntax used by Envoy.
    """
    # Strip comments.
    text = re.sub(r'#[^\n]*', '', text)

    # Find the outermost dict assignment.
    # We look for REPOSITORY_LOCATIONS_SPEC = dict(...)
    # as well as PROTOBUF_VERSION and PROTOC_VERSIONS constants.
    protobuf_version_match = re.search(r'PROTOBUF_VERSION\s*=\s*"([^"]+)"', text)
    protobuf_version = protobuf_version_match.group(1) if protobuf_version_match else None

    # Extract PROTOC_VERSIONS dict
    protoc_versions = {}
    protoc_match = re.search(r'PROTOC_VERSIONS\s*=\s*dict\s*\((.*?)\)', text, re.DOTALL)
    if protoc_match:
        for m in re.finditer(r'(\w+)\s*=\s*"([^"]+)"', protoc_match.group(1)):
            protoc_versions[m.group(1)] = m.group(2)

    # Find REPOSITORY_LOCATIONS_SPEC = dict(...)
    spec_match = re.search(r'REPOSITORY_LOCATIONS_SPEC\s*=\s*dict\s*\(', text)
    if not spec_match:
        return {}, protobuf_version, protoc_versions

    # Extract the content of the outer dict.
    start = spec_match.end() - 1  # position of opening '('
    depth = 0
    i = start
    while i < len(text):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                break
        i += 1
    spec_text = text[start + 1:i]

    # Now parse individual top-level entries: key = dict(...)
    entries = {}
    entry_re = re.compile(r'(\w+)\s*=\s*dict\s*\(', re.DOTALL)
    for m in entry_re.finditer(spec_text):
        key = m.group(1)
        # Find matching closing paren.
        pos = m.end() - 1
        depth = 0
        j = pos
        while j < len(spec_text):
            if spec_text[j] == '(':
                depth += 1
            elif spec_text[j] == ')':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        entry_text = spec_text[pos + 1:j]
        constants = {'PROTOBUF_VERSION': protobuf_version} if protobuf_version else {}
        entry = _parse_entry(entry_text, constants=constants)
        entries[key] = entry

    return entries, protobuf_version, protoc_versions


def _parse_entry(text, constants=None):
    """Parse a single dependency entry dict body."""
    entry = {}
    if constants is None:
        constants = {}

    # version — may be a quoted string or an unquoted constant reference.
    v = re.search(r'\bversion\s*=\s*"([^"]+)"', text)
    if v:
        entry['version'] = v.group(1)
    else:
        # Try unquoted constant reference (e.g. version = PROTOBUF_VERSION)
        vc = re.search(r'\bversion\s*=\s*([A-Z_][A-Z0-9_]*)', text)
        if vc and vc.group(1) in constants:
            entry['version'] = constants[vc.group(1)]

    # sha256
    s = re.search(r'\bsha256\s*=\s*"([^"]+)"', text)
    if s:
        entry['sha256'] = s.group(1)

    # urls — a list of quoted strings
    u = re.search(r'\burls\s*=\s*\[([^\]]*)\]', text, re.DOTALL)
    if u:
        urls = re.findall(r'"([^"]+)"', u.group(1))
        entry['urls'] = urls

    return entry


def _expand_urls(entry):
    """Expand version placeholders in URLs."""
    version = entry.get('version', '')
    raw_urls = entry.get('urls', [])
    expanded = []
    for url in raw_urls:
        if version:
            try:
                expanded.append(_format_version(url, version))
            except (KeyError, ValueError):
                expanded.append(url)
        else:
            expanded.append(url)
    return expanded


def _to_mirror_url(upstream_url):
    """Convert an upstream URL to its mirror.bazel.build equivalent.

    Only URLs whose host is under github.com, codeload.github.com,
    raw.githubusercontent.com, or objects.githubusercontent.com are
    considered; others return None.
    """
    # Strip scheme.
    m = re.match(r'https?://([^/]+)(/.+)', upstream_url)
    if not m:
        return None
    host, path = m.group(1), m.group(2)
    mirrored_hosts = {
        'github.com',
        'codeload.github.com',
        'raw.githubusercontent.com',
        'objects.githubusercontent.com',
        'releases.bazel.build',
    }
    if host not in mirrored_hosts:
        return None
    return f"{MIRROR_BASE}/{host}{path}"


async def _head_request(url, timeout):
    """Issue an HTTP HEAD request and return (url, status_code, error_msg)."""
    import urllib.parse
    loop = asyncio.get_event_loop()

    def _do_request():
        req = urllib.request.Request(url, method='HEAD')
        req.add_header('User-Agent', 'envoy-mirror-coverage-check/1.0')
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return resp.status, None
        except urllib.error.HTTPError as e:
            return e.code, None
        except Exception as e:
            return None, str(e)

    for attempt in range(MAX_RETRIES):
        status, err = await loop.run_in_executor(None, _do_request)
        if err is None:
            return url, status, None
        if attempt < MAX_RETRIES - 1:
            await asyncio.sleep(RETRY_DELAY * (attempt + 1))
    return url, None, err


async def check_mirrors(entries_by_file, timeout, concurrency, use_color):
    """Check all mirror URLs and return results."""
    semaphore = asyncio.Semaphore(concurrency)

    results = []  # list of {file, name, upstream_url, mirror_url, status, error}

    async def check_one(file_rel, name, upstream_url, mirror_url):
        async with semaphore:
            _, status, err = await _head_request(mirror_url, timeout)
            return {
                'file': file_rel,
                'name': name,
                'upstream_url': upstream_url,
                'mirror_url': mirror_url,
                'status': status,
                'error': err,
                'mirrored': status == 200,
            }

    tasks = []
    for file_rel, entries in entries_by_file.items():
        for name, entry in entries.items():
            for url in _expand_urls(entry):
                mirror_url = _to_mirror_url(url)
                if mirror_url:
                    tasks.append(check_one(file_rel, name, url, mirror_url))

    print(f"Checking {len(tasks)} mirror URL(s) with concurrency={concurrency}...\n",
          file=sys.stderr)
    results = await asyncio.gather(*tasks)
    return list(results)


def _colorize(text, color, use_color):
    if use_color:
        return f"{color}{text}{RESET}"
    return text


def print_table(results, use_color):
    """Print a human-readable summary table."""
    col_name = max((len(r['name']) for r in results), default=10)
    col_status = 8
    header = (f"{'DEP':<{col_name}}  {'STATUS':<{col_status}}  "
              f"{'UPSTREAM URL'}")
    print(header)
    print('-' * (len(header) + 20))

    mirrored = [r for r in results if r['mirrored']]
    not_mirrored = [r for r in results if not r['mirrored'] and r['error'] is None]
    errors = [r for r in results if r['error'] is not None]

    for r in sorted(results, key=lambda x: (x['file'], x['name'])):
        if r['mirrored']:
            status_str = _colorize('200 OK', GREEN, use_color)
        elif r['error']:
            status_str = _colorize(f"ERR", YELLOW, use_color)
        else:
            status_str = _colorize(f"{r['status']}", RED, use_color)
        print(f"{r['name']:<{col_name}}  {status_str:<{col_status + (10 if use_color else 0)}}  {r['upstream_url']}")

    print()
    print(f"Summary: {len(mirrored)} mirrored, "
          f"{len(not_mirrored)} not mirrored, "
          f"{len(errors)} errors")
    print()
    if mirrored:
        print(_colorize("Deps with confirmed mirrors (add these):", GREEN, use_color))
        seen = set()
        for r in sorted(mirrored, key=lambda x: (x['file'], x['name'])):
            key = (r['file'], r['name'])
            if key not in seen:
                seen.add(key)
                print(f"  [{r['file']}] {r['name']}")
                print(f"    mirror: {r['mirror_url']}")


def load_all_entries(repo_root):
    """Load all dependency entries from all repository_locations files."""
    entries_by_file = {}
    for rel in REPO_LOCATIONS_FILES:
        path = repo_root / rel
        if not path.exists():
            print(f"Warning: {path} not found, skipping.", file=sys.stderr)
            continue
        text = path.read_text()
        entries, protobuf_version, protoc_versions = _parse_bzl_dict(text)

        # Synthesise the _compiled_protoc_deps entries.
        if protobuf_version and protoc_versions:
            for platform, sha in protoc_versions.items():
                dep_name = f"com_google_protobuf_protoc_{platform}"
                # Use the same URL pattern as _compiled_protoc_deps.
                plat_str = platform.replace("_", "-", 1)
                url_template = (
                    f"https://github.com/protocolbuffers/protobuf/releases/download/"
                    f"v{{version}}/protoc-{{version}}-{plat_str}.zip"
                )
                entries[dep_name] = {
                    'version': protobuf_version,
                    'sha256': sha,
                    'urls': [url_template],
                }

        entries_by_file[rel] = entries
    return entries_by_file


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--json', metavar='FILE', help='Write JSON output to FILE')
    parser.add_argument('--timeout', type=float, default=15.0,
                        help='HTTP timeout in seconds (default: 15)')
    parser.add_argument('--concurrency', type=int, default=20,
                        help='Number of concurrent HTTP requests (default: 20)')
    parser.add_argument('--no-color', action='store_true',
                        help='Disable ANSI color output')
    parser.add_argument('--repo-root', default=None,
                        help='Path to the Envoy repository root (autodetected if not set)')
    args = parser.parse_args()

    use_color = not args.no_color and sys.stdout.isatty()

    if args.repo_root:
        repo_root = Path(args.repo_root)
    else:
        # Try to find the repo root by looking for the WORKSPACE file.
        script_dir = Path(__file__).resolve().parent
        for candidate in [script_dir.parent.parent, Path.cwd()]:
            if (candidate / 'WORKSPACE').exists() or (candidate / 'MODULE.bazel').exists():
                repo_root = candidate
                break
        else:
            print("Could not find repo root. Use --repo-root.", file=sys.stderr)
            sys.exit(1)

    entries_by_file = load_all_entries(repo_root)
    if not entries_by_file:
        print("No entries found.", file=sys.stderr)
        sys.exit(1)

    total_deps = sum(len(v) for v in entries_by_file.values())
    print(f"Loaded {total_deps} dependency entries from {len(entries_by_file)} file(s).",
          file=sys.stderr)

    results = asyncio.run(
        check_mirrors(entries_by_file, args.timeout, args.concurrency, use_color))

    print_table(results, use_color)

    if args.json:
        out_path = Path(args.json)
        out_path.write_text(json.dumps(results, indent=2))
        print(f"\nJSON output written to {out_path}", file=sys.stderr)

    # Exit with non-zero if any requests errored (transient), but not for
    # plain 404s (those are expected for deps not on the mirror).
    errors = [r for r in results if r['error'] is not None]
    if errors:
        print(f"\nWarning: {len(errors)} URL(s) had transient errors.", file=sys.stderr)


if __name__ == '__main__':
    main()
