#!/bin/bash

set -euo pipefail

# Check if this is a PR event

if ! jq -e '.issue.pull_request' "${GITHUB_EVENT_PATH}"; then
  echo "Not a PR... Exiting."
  exit 0
fi

# Check if the comment is "/retest"

if [ "$(jq -r '.comment.body' "${GITHUB_EVENT_PATH}")" != "/retest" ]; then
  echo "Nothing to do... Exiting."
  exit 0
fi

# Get the PR details

PR_URL=$(jq -r '.issue.pull_request.url' "${GITHUB_EVENT_PATH}")

curl --silent --request GET \
  --url "${PR_URL}" \
  --header "authorization: Bearer ${GITHUB_TOKEN}" \
  --header "content-type: application/json" > pr.json

# Extract useful PR info

PR_NUMBER=$(jq -r '.number' pr.json)
PR_BRANCH=$(jq -r '.head.ref' pr.json)
PR_COMMIT=$(jq -r '.head.sha' pr.json)

echo "Running /retest command for PR #${PR_NUMBER}"
echo "PR branch: ${PR_BRANCH}"
echo "Latest PR commit: ${PR_COMMIT}"

# Get failed Workflow runs for the latest PR commit

curl --silent --request GET \
  --url "https://api.github.com/repos/envoyproxy/envoy-mobile/actions/runs?branch=${PR_BRANCH}" \
  --header "authorization: Bearer ${GITHUB_TOKEN}" \
  --header "content-type: application/json" > runs.json
jq ".workflow_runs[] | select(.head_sha == \"${PR_COMMIT}\") | select(.conclusion == \"failure\")" runs.json > failed_runs.json
jq -c ". | {name: .name, url: .url}" failed_runs.json > failed_runs_compact.json

# Iterate over each failed run and retry its failed jobs

while read -r failed_run; do
  echo "$failed_run" > line.json
  RUN_NAME="$(jq -r '.name' line.json)"
  echo "Retrying failed job: ${RUN_NAME}"
  RUN_URL="$(jq -r '.url' line.json)"
  curl --silent --request POST \
    --url "${RUN_URL}/rerun-failed-jobs" \
    --header "authorization: Bearer ${GITHUB_TOKEN}" \
    --header "content-type: application/json"
done < failed_runs_compact.json

# Add a rocket emoji reaction to the "/retest" comment to inform that this script finished running

REACTION_URL="$(jq -r '.comment.url' "${GITHUB_EVENT_PATH}")/reactions"

curl --silent --request POST \
  --url "${REACTION_URL}" \
  --header "authorization: Bearer ${GITHUB_TOKEN}" \
  --header "accept: application/vnd.github.squirrel-girl-preview+json" \
  --header "content-type: application/json" \
  --data '{ "content" : "rocket" }'
