#!/usr/bin/env bash
set -ex

while :; do
  curl -v localhost/service
  sleep 1
done