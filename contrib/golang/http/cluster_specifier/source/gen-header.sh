#!/bin/bash

set -x
set -o errexit

cd go/pkg/http

go tool cgo --exportheader libgolang.h shim.go config.go

ls -lh libgolang.h

cd -

mv go/pkg/http/libgolang.h common/dso/libgolang.h

