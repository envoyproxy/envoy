#!/bin/bash -e

AZP_BRANCH="${AZP_BRANCH:-}"
ENVOY_GIT_USERNAME="${ENVOY_GIT_USERNAME:-envoybot}"
ENVOY_GIT_EMAIL="${ENVOY_GIT_EMAIL:-envoybot@envoyproxy.io}"

MAIN_BRANCH=refs/heads/main


if [[ -z "$AZP_BRANCH" ]]; then
    # shellcheck disable=SC2016
    echo '$AZP_BRANCH must be set, exiting' >&2
    exit 1
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
    local dev_args=()

    if [[ "$AZP_BRANCH" != "$MAIN_BRANCH" ]]; then
        dev_args+=("--patch")
    fi

    bazel run @envoy_repo//:dev -- "${dev_args[@]}"
}

reopen_branch () {
    configure_git_user
    create_dev_commit

    # Not sure if it this stage it can/should push directly to the branch or rather just create a PR
    # create_pr
}

reopen_branch
