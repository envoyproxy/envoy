#!/bin/sh

set -ex

if ! jq -e '.issue.pull_request' ${GITHUB_EVENT_PATH}; then
    echo "Not a PR... Exiting."
    exit 0
fi

if [ "$(jq -r '.comment.body' ${GITHUB_EVENT_PATH})" != "/retest" ]; then
    echo "Nothing to do... Exiting."
    exit 0
fi

PR_URL=$(jq -r '.issue.pull_request.url' ${GITHUB_EVENT_PATH})

curl --request GET \
    --url "${PR_URL}" \
    --header "authorization: Bearer ${GITHUB_TOKEN}" \
    --header "content-type: application/json" > pr.json

ACTOR=$(jq -r '.user.login' pr.json)
BRANCH=$(jq -r '.head.ref' pr.json)

curl --request GET \
    --url "https://api.github.com/repos/${GITHUB_REPOSITORY}/actions/runs?event=pull_request&actor=${ACTOR}&branch=${BRANCH}" \
    --header "authorization: Bearer ${GITHUB_TOKEN}" \
    --header "content-type: application/json" | jq '.workflow_runs | max_by(.run_number)' > run.json

RUN_URL=$(jq -r '.url' run.json)

curl --request POST \
    --url "${RUN_URL}/rerun-failed-jobs" \
    --header "authorization: Bearer ${GITHUB_TOKEN}" \
    --header "content-type: application/json"


REACTION_URL="$(jq -r '.comment.url' ${GITHUB_EVENT_PATH})/reactions"

curl --request POST \
    --url "${REACTION_URL}" \
    --header "authorization: Bearer ${GITHUB_TOKEN}" \
    --header "accept: application/vnd.github.squirrel-girl-preview+json" \
    --header "content-type: application/json" \
    --data '{ "content" : "rocket" }'
