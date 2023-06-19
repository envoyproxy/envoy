#!/bin/bash -e

ENVOY_GIT_USERNAME="${ENVOY_GIT_USERNAME:-envoy-bot}"
ENVOY_GIT_EMAIL="${ENVOY_GIT_EMAIL:-envoy-bot@users.noreply.github.com}"
ENVOY_RELEASE_VERSION="${ENVOY_RELEASE_VERSION:-}"

MAIN_BRANCH=refs/heads/main
MAIN_BRANCH_SHORTNAME=main
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"


if [[ "$CURRENT_BRANCH" != "$MAIN_BRANCH" ]] && [[ "$CURRENT_BRANCH" != "$MAIN_BRANCH_SHORTNAME" ]]; then
    echo "Current branch ($CURRENT_BRANCH) must be \`main\`, exiting" >&2
    exit 1
else
    # TODO(phlax): remove once its clear what this should be
    echo "Current branch: $CURRENT_BRANCH"
fi


configure_git_user () {
    if [[ -z "$ENVOY_GIT_USERNAME" || -z "$ENVOY_GIT_EMAIL" ]]; then
        echo 'Unable to set git name/email, using existing git config' >&2
        return
    fi
    git config --global user.name "$ENVOY_GIT_USERNAME"
    git config --global user.email "$ENVOY_GIT_EMAIL"
}

create_dev_commit () {
    bazel run @envoy_repo//:dev -- --patch
}

get_release_name () {
    local version
    if [[ -z "$ENVOY_RELEASE_VERSION" ]]; then
        version="$(cut -d- -f1 < VERSION.txt | cut -d. -f-2)"
    else
        version="$ENVOY_RELEASE_VERSION"
    fi
    echo -n "release/v${version}"
}

create_branch () {
    local release_name commit_sha
    release_name="$(get_release_name)"
    commit_sha="$(git rev-parse HEAD)"

    echo "Creating ${release_name} from ${commit_sha}"
    git checkout -b "$release_name"
    git push origin "$release_name"
}

create_release_branch () {
    configure_git_user
    create_dev_commit
    create_branch
}

create_release_branch
