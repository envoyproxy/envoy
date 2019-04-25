#!/usr/bin/env bash
set -ex

while :; do
  curl -v localhost:9211/status/200
  sleep 1
done
