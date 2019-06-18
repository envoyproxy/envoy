#!/usr/bin/env bash

set -e

cd $(dirname $0)/..

echo "fetching dependencies..."
go get golang.org/x/net/context
go get golang.org/x/sys/unix
go get google.golang.org/grpc
echo "done"
