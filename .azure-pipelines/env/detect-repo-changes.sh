#!/bin/bash -e

# Only exclude checks in pull requests.
if [[ $BUILD_REASON != "PullRequest" ]]; then
    echo "##vso[task.setvariable variable=mobileOnly;isoutput=true]false"
    echo "##vso[task.setvariable variable=docsOnly;isoutput=true]false"
    echo "##vso[task.setvariable variable=examplesOnly;isoutput=true]false"
    echo "##vso[task.setvariable variable=requirements;isoutput=true]true"
    exit 0
fi

CHANGE_TARGET="origin/$TARGET_BRANCH"

echo "Comparing changes ${CHANGE_TARGET}...HEAD"
CHANGED_PATHS=$(git diff --name-only "${CHANGE_TARGET}"...HEAD | cut -d/ -f1 | sort -u | jq -sR 'rtrimstr("\n") | split("\n")')
echo "$CHANGED_PATHS" | jq '.'
CHANGED_PATH_COUNT=$(echo "$CHANGED_PATHS" | jq '. | length')
CHANGED_MOBILE_ONLY=false
CHANGED_DOCS_ONLY=false
CHANGED_EXAMPLES_ONLY=false

CHANGED_MOBILE=$(echo "$CHANGED_PATHS" | jq '. as $A | "mobile" | IN($A[])')
if [[ $CHANGED_MOBILE == true && $CHANGED_PATH_COUNT -eq 1 ]]; then
    CHANGED_MOBILE_ONLY=true
fi
CHANGED_DOCS=$(echo "$CHANGED_PATHS" | jq '. as $A | "docs" | IN($A[])')
if [[ $CHANGED_DOCS == true && $CHANGED_PATH_COUNT -eq 1 ]]; then
    CHANGED_DOCS_ONLY=true
fi
CHANGED_EXAMPLES=$(echo "$CHANGED_PATHS" | jq '. as $A | "examples" | IN($A[])')
if [[ $CHANGED_EXAMPLES == true && $CHANGED_PATH_COUNT -eq 1 ]]; then
    CHANGED_EXAMPLES_ONLY=true
fi

echo "##vso[task.setvariable variable=mobileOnly;isoutput=true]${CHANGED_MOBILE_ONLY}"
echo "##vso[task.setvariable variable=docsOnly;isoutput=true]${CHANGED_DOCS_ONLY}"
echo "##vso[task.setvariable variable=examplesOnly;isoutput=true]${CHANGED_EXAMPLES_ONLY}"
