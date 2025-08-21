#!/usr/bin/env bash

set -euo pipefail

symbolgraph_dir="${1:-}"
if [[ -z "$symbolgraph_dir" ]]; then
  ./bazelw build //library/swift:ios_lib --config=release-ios --output_groups=+swift_symbol_graph
  symbolgraph_dir="bazel-bin/library/swift/ios_lib.symbolgraph"
fi

"$(xcrun --find docc)" convert \
  --index \
  --fallback-display-name Envoy \
  --fallback-bundle-identifier io.envoyproxy.EnvoyMobile \
  --fallback-bundle-version "$(cat VERSION)" \
  --output-dir Envoy.doccarchive \
  --additional-symbol-graph-dir "$symbolgraph_dir"
