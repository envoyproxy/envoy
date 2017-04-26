PHONY: all

ENVOY_TAG ?= "latest"
ENVOY_BUILD_DIR ?= "/tmp/envoy-docker-build"
ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

all: binary container test

binary:
	echo "CWD: ${ROOT_DIR}"
	rm -rf ./bin
	mkdir -p ./bin/stripped
	mkdir -p ./bin/debug
	mkdir -p ./bin/configs
	docker run -t -i --name build-envoy-alpine-ct -v ${ROOT_DIR}/../../:/source -v $(ENVOY_BUILD_DIR):/build lyft/envoy-build:latest /bin/bash -c \
		"set -x && cd /source &&
		cp /build/envoy/source/exe/envoy /tmp/envoy && \
		cd /tmp/ && strip envoy && \
		mv /tmp/envoy /source/ci/build_alpine_container/bin/stripped/ && \
		cp /source/build_normal/source/exe/envoy /source/ci/build_alpine_container/bin/debug/ && \
		cp /source/configs/google_com_proxy.json /source/ci/build_alpine_container/bin/configs/"
	docker rm -f build-envoy-alpine-ct
	# Fail build if a core file gets generated during the build.
	if [ -f "../../core" ]; \
	then \
		echo "Core dumped, failing build"; \
		exit 1; \
	fi

container:
	docker build -f ./Dockerfile-envoy-alpine -t lyft/envoy-alpine:${ENVOY_TAG} .
	docker build -f ./Dockerfile-envoy-alpine-debug -t lyft/envoy-alpine-debug:${ENVOY_TAG} .

test:
	./run_alpine_binary_verification.sh debug
	./run_alpine_binary_verification.sh stripped

clean:
	rm -rf ./bin
