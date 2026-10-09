#!/usr/bin/env bash

set -e -o pipefail

task="${1:-}"
dependency="${2:-}"
version="${3:-}"

version_short="$version"
if [[ "$version" =~ ^[0-9a-fA-F]{40}$ ]]; then
    version_short="${version:0:7}"
fi
target="${version_short:-latest}"

case "$task" in
    bazel)
        dependency_name="$dependency"
        if [[ -z "$dependency" ]]; then
            echo '::error::dependency is required for the bazel task' >&2
            exit 1
        fi
        printf -v dependency_arg '%q' "${dependency}${version:+=$version}"
        command="./ci/do_ci.sh deps.update ${dependency_arg}"
        ;;
    registry)
        dependency_name=bazel-registry
        command='./ci/do_ci.sh registry'
        ;;
    lockfiles)
        dependency_name=lockfiles
        command='./ci/do_ci.sh lockfiles'
        ;;
    *)
        echo "Usage: $0 <bazel|registry|lockfiles> [dependency] [version]" >&2
        exit 1
        ;;
esac

echo "Updating(${task}): ${dependency_name} -> ${target}" >&2
./ci/run_envoy_docker.sh "$command" >&2

if [[ -n "$version_short" ]]; then
    OUTPUT="$version_short"
elif [[ "$task" == registry ]]; then
    OUTPUT=$(sed -n -E \
        's#^common --registry=https://raw\.githubusercontent\.com/envoyproxy/bazel-registry/([0-9a-f]+)$#\1#p' \
        .bazelrc | cut -c1-7)
elif [[ "$task" == lockfiles ]]; then
    if [[ -z "$(git status --porcelain -- ':(glob)**/MODULE.bazel.lock')" ]]; then
        echo 'Lockfiles are in sync, nothing to do' >&2
        OUTPUT=in-sync
    else
        OUTPUT=$(git diff -- ':(glob)**/MODULE.bazel.lock' | sha256sum | cut -c1-7)
    fi
else
    OUTPUT=$(git diff -U0 -- ':(glob)**/MODULE.bazel' \
        | sed -n -E "s/^\+bazel_dep\(name = \"${dependency}\", version = \"([^\"]+)\".*/\1/p" \
        | head -n1)
fi

if [[ -z "$OUTPUT" ]]; then
    echo "::error::Unable to determine updated version for ${dependency_name}" >&2
    exit 1
fi

echo "$OUTPUT"
