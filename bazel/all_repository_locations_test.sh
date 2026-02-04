#!/usr/bin/env bash

set -euo pipefail

python3 - "$1" <<'PY'
import json
import pathlib
import sys

deps = json.loads(pathlib.Path(sys.argv[1]).read_text())

abseil = deps["abseil_cpp"]
assert abseil["version"] == "20260107.1", abseil
assert abseil["module_url"] == "https://bcr.bazel.build/modules/abseil-cpp/20260107.1/", abseil
assert abseil["urls"] == [abseil["module_url"]], abseil

protobuf = deps["com_google_protobuf"]
assert protobuf["version"] == "35.1.bcr.envoy", protobuf
assert protobuf["module_url"].endswith("/modules/protobuf/35.1.bcr.envoy/"), protobuf
assert "raw.githubusercontent.com/envoyproxy/toolshed/" in protobuf["module_url"], protobuf

quiche = deps["quiche"]
assert quiche["version"] == "89d6d17edc0f0b79f38edf6fac9e5c8bf5f3cfd7", quiche
assert "module_url" not in quiche, quiche
assert quiche["urls"] == ["https://github.com/google/quiche/archive/89d6d17edc0f0b79f38edf6fac9e5c8bf5f3cfd7.tar.gz"], quiche

jsonschema = deps["com_github_chrusty_protoc_gen_jsonschema"]
assert jsonschema["version"] == "7680e4998426e62b6896995ff73d4d91cc5fb13c", jsonschema
assert "module_url" not in jsonschema, jsonschema
assert jsonschema["urls"] == ["https://github.com/norbjd/protoc-gen-jsonschema/archive/7680e4998426e62b6896995ff73d4d91cc5fb13c.zip"], jsonschema
PY
