BUILD_IMAGE  = golang:1.18
PROJECT_NAME = envoy-go-extension

build-local: sync
	go build \
		-v \
		--buildmode=c-shared \
		-o libgolang.so \
		.

build:
	docker run --rm -v $(shell pwd):/go/src/${PROJECT_NAME} -w /go/src/${PROJECT_NAME} ${BUILD_IMAGE} make build-local

sync:
	cp export.go pkg/api/export.txt

clean:
	@rm -rf libgolang.*

.PHONY: sync build-local build
