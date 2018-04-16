#!/bin/bash

set -e

# TODO(htuch): Remove this once we've verified this script works.
set -x

CHECKOUT_DIR=../data-plane-api

if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  echo "Cloning..."
  git clone git@github.com:envoyproxy/data-plane-api "$CHECKOUT_DIR"

  git -C "$CHECKOUT_DIR" config user.name "data-plane-api(CircleCI)"
  git -C "$CHECKOUT_DIR" config user.email data-plane-api@users.noreply.github.com
  git -C "$CHECKOUT_DIR" fetch
  git -C "$CHECKOUT_DIR" checkout -B master origin/master

  # Determine last envoyproxy/envoy SHA in envoyproxy/data-plane-api
  MIRROR_MSG="Mirrored from https://github.com/envoyproxy/envoy"
  LAST_ENVOY_SHA=$(git -C "$CHECKOUT_DIR" log --grep="$MIRROR_MSG" -n 1 | grep "$MIRROR_MSG" | \
    tail -n 1 | sed -e "s#.*$MIRROR_MSG @ ##")

  echo "Last mirrored envoyproxy/envoy SHA is $LAST_ENVOY_SHA"

  # Compute SHA sequence to replay in envoyproxy/data-plane-api
  SHAS=$(git rev-list --reverse "$LAST_ENVOY_SHA"..HEAD api/)

  # For each SHA, hard reset, rsync api/ and generate commit in
  # envoyproxy/data-plane-api
  API_WORKING_DIR="../envoy-api-mirror"
  git worktree add "$API_WORKING_DIR"
  for sha in $SHAS
  do
    git -C "$API_WORKING_DIR" reset --hard "$sha"
    COMMIT_MSG=$(git -C "$API_WORKING_DIR" log --format=%B -n 1)
    QUALIFIED_COMMIT_MSG=$(echo -e "$COMMIT_MSG\n\n$MIRROR_MSG @ $sha")
    rsync -acv --delete --exclude "ci/" --exclude ".*" --exclude LICENSE \
      --exclude WORKSPACE \
      "$API_WORKING_DIR"/api/ "$CHECKOUT_DIR"/
    git -C "$CHECKOUT_DIR" add .
    git -C "$CHECKOUT_DIR" commit -m "$QUALIFIED_COMMIT_MSG"
  done

  echo "Pushing..."
  git -C "$CHECKOUT_DIR" push origin master
  echo "Done"
fi
