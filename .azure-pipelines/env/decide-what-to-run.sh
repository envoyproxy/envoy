#!/bin/bash -e

RUN_BUILD=true
RUN_CHECKS=true
RUN_DOCKER=true
RUN_PACKAGING=true
RUN_RELEASE_TESTS=true

if [[ $CHANGED_MOBILEONLY == true || $CHANGED_DOCSONLY == true ]]; then
    RUN_BUILD=false
    RUN_CHECKS=false
    RUN_DOCKER=false
    RUN_PACKAGING=false
fi
if [[ $CHANGED_EXAMPLESONLY == true ]]; then
    RUN_CHECKS=false
fi
if [[ "$ISSTABLEBRANCH" == True && -n "$POSTSUBMIT" && $STATE_ISDEV == false ]]; then
    RUN_RELEASE_TESTS=false
fi

# Run ~everything in postsubmit
if [[ $BUILD_REASON != "PullRequest" ]]; then
    echo "##vso[task.setvariable variable=build;isoutput=true]true"
    echo "##vso[task.setvariable variable=checks;isoutput=true]true"
    echo "##vso[task.setvariable variable=docker;isoutput=true]true"
    echo "##vso[task.setvariable variable=packaging;isoutput=true]true"
    echo "##vso[task.setvariable variable=releaseTests;isoutput=true]${RUN_RELEASE_TESTS}"
    exit 0
fi

echo "##vso[task.setvariable variable=build;isoutput=true]${RUN_BUILD}"
echo "##vso[task.setvariable variable=checks;isoutput=true]${RUN_CHECKS}"
echo "##vso[task.setvariable variable=docker;isoutput=true]${RUN_DOCKER}"
echo "##vso[task.setvariable variable=packaging;isoutput=true]${RUN_PACKAGING}"
echo "##vso[task.setvariable variable=releaseTests;isoutput=true]${RUN_RELEASE_TESTS}"
