#!/usr/bin/env python3

import json
import pathlib
import sys

original = json.loads(pathlib.Path(sys.argv[1]).read_text())
shaped = json.loads(pathlib.Path(sys.argv[2]).read_text())

assert shaped.keys() == {name for name, dep in original.items() if dep.get("version") is not None}
for name, dep in original.items():
    if dep.get("version") is None:
        continue
    source = dep.get("source") or {}
    expected = dict(dep, sha256=source.get("integrity") or "")
    if source.get("url"):
        expected["urls"] = [source["url"]] + (source.get("mirror_urls") or [])
    assert shaped[name] == expected, name
    assert isinstance(shaped[name]["sha256"], str), name
    assert isinstance(shaped[name]["urls"], list), name

for name in ("abseil-cpp", "grpc", "protobuf", "boringssl", "quiche"):
    dep = shaped[name]
    assert dep["urls"][0].startswith("https://github.com/"), (name, dep)
    assert "/archive/" in dep["urls"][0] or "/releases/download/" in dep["urls"][0], (name, dep)
    assert all(isinstance(dep[key], str) for key in ("version", "release_date", "sha256")), name
