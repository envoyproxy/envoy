#!/bin/bash -e

PUBLISH_GITHUB_RELEASE=$(run.packaging)
PUBLISH_DOCKERHUB=false

if [[ "$ISSTABLEBRANCH" == True && -n "$POSTSUBMIT" && "$NOSYNC" != true ]]; then
    # main
    if [[ "$ISMAIN" == True ]]; then
        # Update the Dockerhub README
        PUBLISH_DOCKERHUB=true
    # Not main, and not -dev
    elif [[ "$(state.isDev)" == false ]]; then
        if [[ "$(state.versionPatch)" -eq 0 ]]; then
            # A just-forked branch
            PUBLISH_GITHUB_RELEASE=false
        fi
    fi
fi

# Only run Envoy release CI if explictly set
if { [[ "$PRESUBMIT" != 'true' ]] && [[ "$POSTSUBMIT" != 'true' ]]; } || [[ $(Build.Reason) == "Schedule" ]]; then
    PUBLISH_GITHUB_RELEASE=false
fi

echo "##vso[task.setvariable variable=githubRelease;isoutput=true]${PUBLISH_GITHUB_RELEASE}"
echo "##vso[task.setvariable variable=dockerhub;isoutput=true]${PUBLISH_DOCKERHUB}"