
BUILD_IMAGE  = golang:1.14.13
PROJECT_NAME = mosn.io/envoy-go-extension

build-local:
	go build \
		-v \
		--buildmode=c-shared \
		-o libgolang.so \
		.

build:
	docker run --rm -v $(shell pwd):/go/src/${PROJECT_NAME} -w /go/src/${PROJECT_NAME} ${BUILD_IMAGE} make build-local

.PHONY: build-local build
