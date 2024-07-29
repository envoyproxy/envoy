#!/bin/bash -e

PUBLISH_GITHUB_RELEASE=$RUN_PACKAGING
PUBLISH_DOCKERHUB=false

if [[ "$ISSTABLEBRANCH" == True && -n "$POSTSUBMIT" && "$NOSYNC" != true ]]; then
    # main
    if [[ "$ISMAIN" == True ]]; then
        # Update the Dockerhub README
        PUBLISH_DOCKERHUB=true
    # Not main, and not -dev
    elif [[ $STATE_ISDEV == false ]]; then
        if [[ $STATE_VERSIONPATCH -eq 0 ]]; then
            # A just-forked branch
            PUBLISH_GITHUB_RELEASE=false
        fi
    fi
fi

# Only run Envoy release CI if explictly set
if { [[ "$PRESUBMIT" != 'true' ]] && [[ "$POSTSUBMIT" != 'true' ]]; } || [[ $BUILD_REASON == "Schedule" ]]; then
    PUBLISH_GITHUB_RELEASE=false
fi

echo "##vso[task.setvariable variable=githubRelease;isoutput=true]${PUBLISH_GITHUB_RELEASE}"
echo "##vso[task.setvariable variable=dockerhub;isoutput=true]${PUBLISH_DOCKERHUB}"
