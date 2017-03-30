PHONY: all

ENVOY_TAG?="latest"

all: binary container test

binary:
	rm -rf ./bin
	mkdir -p ./bin/stripped
	mkdir -p ./bin/configs
	docker run -t -i --name build-envoy-alpine-ct -v ${PWD}/../../:/source lyft/envoy-build:latest /bin/bash -c \
		"set -x && cd /source && /source/ci/do_ci.sh normal && \
		cp /source/build_normal/source/exe/envoy /tmp/ && \
		cd /tmp/ && strip envoy && \
		cp /tmp/envoy /source/ci/build_alpine_container/bin/stripped/ && \
		cp /source/configs/google_com_proxy.json /source/ci/build_alpine_container/bin/configs/"
	docker rm -f build-envoy-alpine-ct
	# Remove any core file if it gets generated during the build, for now.
	rm -f ../../core

container:
	docker build -f ./Dockerfile-alpine-envoy -t envoy-alpine:${ENVOY_TAG} .

test:
	./run_alpine_binary_verification.sh

clean:
	rm -rf ./bin
