#!/usr/bin/env bash

# NOTE: this cannot live inside bazel as it calls bazel query with wildcard expressions

set -euo pipefail

CURRENT_SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
ENVOY_SRCDIR="${ENVOY_SRCDIR:-$(realpath "${CURRENT_SCRIPT_DIR}/../..")}"
JQ_BIN="${JQ_BIN:-jq}"

if [[ -n "${BAZEL_BUILD_OPTION_LIST:-}" ]]; then
  read -r -a BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST}"
else
  BAZEL_BUILD_OPTIONS=(--config=clang)
fi

if [[ -n "${BAZEL_QUERY_OPTION_LIST:-}" ]]; then
  read -r -a BAZEL_QUERY_OPTIONS <<< "${BAZEL_QUERY_OPTION_LIST}"
else
  BAZEL_QUERY_OPTIONS=(--config=clang)
fi

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

cd "${ENVOY_SRCDIR}"

query_repo_names() {
  bazel query "${BAZEL_QUERY_OPTIONS[@]}" "$1" \
    | sed -n 's/^@*\([^/]*\)\/\/.*/\1/p' \
    | sort -u
}

json_array_from_lines() {
  "${JQ_BIN}" -Rsc 'split("\n") | map(select(length > 0))'
}

dependency_json_path() {
  bazel cquery "${BAZEL_BUILD_OPTIONS[@]}" --output=files "$1" | tail -n1
}

echo "Validating dependency graph structure..."

graph_gap_repos_path="${tmpdir}/graph_gap_repos.txt"
query_repo_names \
  'filter("^@", deps(//source/...)) except filter("^@", deps(//source/exe:envoy_main_common_with_core_extensions_lib)) except filter("^@", deps(//source/extensions/...))' \
  > "${graph_gap_repos_path}"

if [[ -s "${graph_gap_repos_path}" ]]; then
  {
    echo "Dependency graph structure validation failed."
    echo "The following external deps are reachable from //source/... but not from either"
    echo "//source/exe:envoy_main_common_with_core_extensions_lib or //source/extensions/..."
    echo "(that means the owning //source target is outside the declared reachability roots):"
    sed 's/^/  /' "${graph_gap_repos_path}"
    echo "Wire the owning //source target into the core root or //source/extensions/...,"
    echo "or update this query-based graph-structure check if the intended root set changed."
  } >&2
  exit 1
fi

echo "Validating marginal //test/... dependencies are declared test_only..."
bazel build "${BAZEL_BUILD_OPTIONS[@]}" --remote_download_toplevel //tools/dependency:filtered-dependencies >/dev/null
filtered_dependencies_path="$(dependency_json_path //tools/dependency:filtered-dependencies)"

test_only_repos_path="${tmpdir}/test_only_repos.txt"
"${JQ_BIN}" -r \
  'to_entries[]
   | select((.value.use_category // []) | index("test_only"))
   | .key' \
  "${filtered_dependencies_path}" \
  | sort -u \
  > "${test_only_repos_path}"

marginal_test_repos_path="${tmpdir}/marginal_test_repos.txt"
query_repo_names \
  'filter("^@", deps(//test/...)) except (filter("^@", deps(//source/...)) except filter("^@", deps(//source/extensions/dynamic_modules/sdk/cpp/...)))' \
  > "${marginal_test_repos_path}"

marginal_test_repos_json_path="${tmpdir}/marginal_test_repos.json"
json_array_from_lines < "${marginal_test_repos_path}" > "${marginal_test_repos_json_path}"

test_only_repos_json_path="${tmpdir}/test_only_repos.json"
json_array_from_lines < "${test_only_repos_path}" > "${test_only_repos_json_path}"

unexpected_test_repos_json_path="${tmpdir}/unexpected_test_repos.json"
# shellcheck disable=SC2016
"${JQ_BIN}" -n \
  --slurpfile marginal "${marginal_test_repos_json_path}" \
  --slurpfile test_only "${test_only_repos_json_path}" \
  '
  $marginal[0]
  | map(select(. != "openssl"))
  | map(select((startswith("raze__")
                or startswith("cu__")
                or . == "cu"
                or startswith("remotejdk")
                or contains("_pip3")) | not))
  | map(select(($test_only[0] | index(.)) | not))
  ' \
  > "${unexpected_test_repos_json_path}"

if [[ "$("${JQ_BIN}" 'length' "${unexpected_test_repos_json_path}")" != "0" ]]; then
  {
    echo "Test-only dependency validation failed."
    echo "The following external deps are reachable from //test/... but not //source/...,"
    echo "and are not declared test_only in bazel/deps.yaml or api/bazel/deps.yaml:"
    "${JQ_BIN}" -r '.[] | "  " + .' "${unexpected_test_repos_json_path}"
    echo "Add use_category: [test_only] for each dep above in bazel/deps.yaml or"
    echo "api/bazel/deps.yaml, or extend validate_graph_structure.sh's allowlist"
    echo "with a documented rationale if the dep is intentionally exempt."
  } >&2
  exit 1
fi
