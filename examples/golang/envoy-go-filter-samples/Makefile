API_NAME = vendor/github.com/envoyproxy/envoy/contrib/golang/filters/http/source/go/pkg/api/
BUILD_IMAGE  = golang:1.18
PROJECT_NAME = envoy-go-extension

build-local:
	go mod vendor
	cp ${API_NAME}/export.txt ./export.go
	go build \
		-v \
		--buildmode=c-shared \
		-o libgolang.so \
		.
	@rm -rf vendor

build:
	docker run --rm -v $(shell pwd):/go/src/${PROJECT_NAME} -w /go/src/${PROJECT_NAME} ${BUILD_IMAGE} make build-local

clean:
	@rm -rf ./export.go libgolang.*

.PHONY: build-local build
