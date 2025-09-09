#!/usr/bin/env bash

# This file was imported from https://github.com/bazelbuild/bazel at d6fec93.

# This script will be run bazel when building process starts to
# generate key-value information that represents the status of the
# workspace. The output should be like
#
# KEY1 VALUE1
# KEY2 VALUE2
#
# If the script exits with non-zero code, it's considered as a failure
# and the output will be discarded.

# For Envoy in particular, we want to force binaries to relink when the Git
# SHA changes (https://github.com/envoyproxy/envoy/issues/2551). This can be
# done by prefixing keys with "STABLE_". To avoid breaking compatibility with
# other status scripts, this one still echos the non-stable ("volatile") names.

# If this SOURCE_VERSION file exists then it must have been placed here by a
# distribution doing a non-git, source build.
# Distributions would be expected to echo the commit/tag as BUILD_SCM_REVISION
if [ -f SOURCE_VERSION ]
then
    echo "BUILD_SCM_REVISION $(cat SOURCE_VERSION)"
    echo "ENVOY_BUILD_SCM_REVISION $(cat SOURCE_VERSION)"
    echo "STABLE_BUILD_SCM_REVISION $(cat SOURCE_VERSION)"
    echo "BUILD_SCM_STATUS Distribution"
    exit 0
fi

if [[ -e ".BAZEL_FAKE_SCM_REVISION" ]]; then
    BAZEL_FAKE_SCM_REVISION="$(cat .BAZEL_FAKE_SCM_REVISION)"
    echo "BUILD_SCM_REVISION $BAZEL_FAKE_SCM_REVISION"
    echo "ENVOY_BUILD_SCM_REVISION $BAZEL_FAKE_SCM_REVISION"
    echo "STABLE_BUILD_SCM_REVISION $BAZEL_FAKE_SCM_REVISION"
else
    # The code below presents an implementation that works for git repository
    git_rev=$(git rev-parse HEAD) || exit 1
    echo "BUILD_SCM_REVISION ${git_rev}"
    echo "ENVOY_BUILD_SCM_REVISION ${git_rev}"
    echo "STABLE_BUILD_SCM_REVISION ${git_rev}"
fi

# If BAZEL_VOLATILE_DIRTY is set then stamped builds will rebuild uncached when
# either a tracked file changes or an untracked file is added or removed.
# Otherwise this just tracks changes to tracked files.
tracked_hash="$(git ls-files -s | sha256sum | head -c 40)"
if [[ -n "$BAZEL_VOLATILE_DIRTY" ]]; then
    porcelain_status="$(git status --porcelain | sha256sum)"
    diff_status="$(git --no-pager diff | sha256sum)"
    tree_hash="$(echo "${tracked_hash}:${porcelain_status}:${diff_status}" | sha256sum | head -c 40)"
    echo "BUILD_SCM_HASH ${tree_hash}"
else
    echo "BUILD_SCM_HASH ${tracked_hash}"
fi

# Check whether there are any uncommitted changes
tree_status="Clean"
git diff-index --quiet HEAD -- || {
    tree_status="Modified"
}

echo "BUILD_SCM_STATUS ${tree_status}"
echo "STABLE_BUILD_SCM_STATUS ${tree_status}"

git_branch=$(git rev-parse --abbrev-ref HEAD)
echo "BUILD_SCM_BRANCH ${git_branch}"

git_remote=$(git remote get-url origin)
if [[ -n "$git_remote" ]]; then
    echo "BUILD_SCM_REMOTE ${git_remote}"
fi
