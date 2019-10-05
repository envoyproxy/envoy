#!/bin/bash

set -e

if [ -z "$CIRCLE_PULL_REQUEST" ] && [ "$CIRCLE_BRANCH" == "master" ]
then
  tools/api/generate_go_protobuf.py
fi
