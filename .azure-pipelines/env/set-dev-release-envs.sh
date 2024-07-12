#!/bin/bash -e

VERSION_DEV=$(cut -d- -f2 VERSION.txt)
VERSION_PATCH=$(cut -d- -f1 VERSION.txt | rev | cut -d. -f1 | rev)
echo "##vso[task.setvariable variable=versionPatch;isoutput=true]$VERSION_PATCH"
if [[ $VERSION_DEV == "dev" ]]; then
    echo "##vso[task.setvariable variable=isDev;isoutput=true]true"
else
    if [[ $BUILD_REASON == "PullRequest" ]]; then
        # Check to make sure that it was this PR that changed the version, otherwise fail
        # as the branch needs to be reopened first.
        # NB: this will not stop a PR that has already passed checks from being landed.
        DIFF_TARGET_BRANCH="origin/$TARGET_BRANCH"
        DIFF_REF=$(git merge-base HEAD "${DIFF_TARGET_BRANCH}")
        CHANGES=$(git diff "$DIFF_REF" HEAD VERSION.txt)
        if [[ -z "$CHANGES" ]]; then
            echo "VERSION.txt is not a development version. Please re-open the branch before making further changes" >&2
            exit 1
        fi
    fi
    echo "##vso[task.setvariable variable=isDev;isoutput=true]false"
fi
