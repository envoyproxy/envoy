#!/usr/bin/env python3

import json
import pathlib
import re
import sys

original = json.loads(pathlib.Path(sys.argv[1]).read_text())
shaped = json.loads(pathlib.Path(sys.argv[2]).read_text())

github_archive = re.compile(r"https://github.com/[^/]+/[^/]+/archive/(.*)\.(?:tar\.gz|zip)")
expected_deps = {}
for name, dep in original.items():
    if dep.get("version") is None:
        continue
    source = dep.get("source") or {}
    expected = dict(dep, sha256=source.get("integrity") or "")
    if source.get("url"):
        url = re.sub(
            r"^(https://github.com/[^/]+/[^/]+/archive/)refs/tags/(.+\.(?:tar\.gz|zip))$",
            r"\1\2", source["url"])
        expected["urls"] = [url] + (source.get("mirror_urls") or [])
    archive = github_archive.fullmatch((expected.get("urls") or [""])[0])
    if archive and "/" in archive[1]:
        continue
    expected_deps[name] = expected

assert shaped.keys() == expected_deps.keys()
for name, dep in shaped.items():
    assert dep == expected_deps[name], name
    assert isinstance(dep["sha256"], str), name
    assert isinstance(dep["urls"], list), name
    url = dep["urls"][0]
    if url.startswith("https://github.com/"):
        assert "/archive/refs/tags/" not in url, name
    archive = github_archive.fullmatch(url)
    assert not archive or "/" not in archive[1], name

if original.get("vectorscan", {}).get("version") is not None:
    assert "vectorscan" not in shaped

for name in ("abseil-cpp", "grpc", "protobuf", "boringssl", "quiche"):
    dep = shaped[name]
    assert dep["urls"][0].startswith("https://github.com/"), (name, dep)
    assert "/archive/" in dep["urls"][0] or "/releases/download/" in dep["urls"][0], (name, dep)
    assert all(isinstance(dep[key], str) for key in ("version", "release_date", "sha256")), name
