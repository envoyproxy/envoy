#!/usr/bin/env bash

set -e

cd $(dirname $0)/..

export GOOS=linux
export GOARCH=amd64
export CGO_ENABLED=0

go build -o ./bin/service ./service

