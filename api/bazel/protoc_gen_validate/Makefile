empty :=
space := $(empty) $(empty)
PACKAGE := github.com/envoyproxy/protoc-gen-validate

# protoc-gen-go parameters for properly generating the import path for PGV
VALIDATE_IMPORT := Mvalidate/validate.proto=${PACKAGE}/validate
GO_IMPORT_SPACES := ${VALIDATE_IMPORT},\
	Mgoogle/protobuf/any.proto=google.golang.org/protobuf/types/known/anypb,\
	Mgoogle/protobuf/duration.proto=google.golang.org/protobuf/types/known/durationpb,\
	Mgoogle/protobuf/struct.proto=google.golang.org/protobuf/types/known/structpb,\
	Mgoogle/protobuf/timestamp.proto=google.golang.org/protobuf/types/known/timestamppb,\
	Mgoogle/protobuf/wrappers.proto=google.golang.org/protobuf/types/known/wrapperspb,\
	Mgoogle/protobuf/descriptor.proto=google.golang.org/protobuf/types/descriptorpb
GO_IMPORT:=$(subst $(space),,$(GO_IMPORT_SPACES))

.DEFAULT_GOAL := help

.PHONY: help
help: Makefile
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

.PHONY: build
build: validate/validate.pb.go ## generates the PGV binary and installs it into $$GOPATH/bin
	go install .

.PHONY: bazel
bazel: ## generate the PGV plugin with Bazel
	bazel build //cmd/... //tests/...

.PHONY: build_generation_tests
build_generation_tests:
	bazel build //tests/generation/...

.PHONY: gazelle
gazelle: ## runs gazelle against the codebase to generate Bazel BUILD files
	bazel run //:gazelle -- update-repos -from_file=go.mod -prune -to_macro=dependencies.bzl%go_third_party
	bazel run //:gazelle

.PHONY: lint
lint: bin/golangci-lint ## lints the package for common code smells
	$(shell pwd)/bin/golangci-lint run ./...
	# lints the python code for style enforcement
	flake8 --config=python/setup.cfg python/protoc_gen_validate/validator.py
	isort --check-only python/protoc_gen_validate/validator.py

bin/golangci-lint:
	GOBIN=$(shell pwd)/bin go install github.com/golangci/golangci-lint/cmd/golangci-lint@v1.54.2

bin/protoc-gen-go:
	GOBIN=$(shell pwd)/bin go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.30.0

bin/harness:
	cd tests && go build -o ../bin/harness ./harness/executor

.PHONY: harness
harness: testcases tests/harness/go/harness.pb.go tests/harness/go/main/go-harness tests/harness/cc/cc-harness bin/harness ## runs the test harness, validating a series of test cases in all supported languages
	./bin/harness -go -cc

.PHONY: bazel-tests
bazel-tests: ## runs all tests with Bazel
	bazel test //tests/... --test_output=errors

.PHONY: example-workspace
example-workspace: ## run all tests in the example workspace
	cd example-workspace && bazel test //... --test_output=errors

.PHONY: testcases
testcases: bin/protoc-gen-go ## generate the test harness case protos
	rm -r tests/harness/cases/go || true
	mkdir tests/harness/cases/go
	rm -r tests/harness/cases/other_package/go || true
	mkdir tests/harness/cases/other_package/go
	rm -r tests/harness/cases/yet_another_package/go || true
	mkdir tests/harness/cases/yet_another_package/go
	rm -r tests/harness/cases/sort/go || true
	mkdir tests/harness/cases/sort/go
	# protoc-gen-go makes us go a package at a time
	cd tests/harness/cases/other_package && \
	protoc \
		-I . \
		-I ../../../.. \
		--go_out="module=${PACKAGE}/tests/harness/cases/other_package/go,${GO_IMPORT}:./go" \
		--plugin=protoc-gen-go=$(shell pwd)/bin/protoc-gen-go \
		--validate_out="module=${PACKAGE}/tests/harness/cases/other_package/go,lang=go:./go" \
		./*.proto
	cd tests/harness/cases/yet_another_package && \
	protoc \
		-I . \
		-I ../../../.. \
		--go_out="module=${PACKAGE}/tests/harness/cases/yet_another_package/go,${GO_IMPORT}:./go" \
		--plugin=protoc-gen-go=$(shell pwd)/bin/protoc-gen-go \
		--validate_out="module=${PACKAGE}/tests/harness/cases/yet_another_package/go,lang=go:./go" \
		./*.proto
	cd tests/harness/cases/sort && \
	protoc \
		-I . \
		-I ../../../.. \
		--go_out="module=${PACKAGE}/tests/harness/cases/sort/go,${GO_IMPORT}:./go" \
		--plugin=protoc-gen-go=$(shell pwd)/bin/protoc-gen-go \
		--validate_out="module=${PACKAGE}/tests/harness/cases/sort/go,lang=go:./go" \
		./*.proto
	cd tests/harness/cases && \
	protoc \
		-I . \
		-I ../../.. \
		--go_out="module=${PACKAGE}/tests/harness/cases/go,Mtests/harness/cases/other_package/embed.proto=${PACKAGE}/tests/harness/cases/other_package/go;other_package,Mtests/harness/cases/yet_another_package/embed.proto=${PACKAGE}/tests/harness/cases/yet_another_package/go,${GO_IMPORT}:./go" \
		--plugin=protoc-gen-go=$(shell pwd)/bin/protoc-gen-go \
		--validate_out="module=${PACKAGE}/tests/harness/cases/go,lang=go,Mtests/harness/cases/other_package/embed.proto=${PACKAGE}/tests/harness/cases/other_package/go,Mtests/harness/cases/yet_another_package/embed.proto=${PACKAGE}/tests/harness/cases/yet_another_package/go:./go" \
		./*.proto

validate/validate.pb.go: bin/protoc-gen-go validate/validate.proto
	protoc -I . \
		--plugin=protoc-gen-go=$(shell pwd)/bin/protoc-gen-go \
		--go_opt=paths=source_relative \
		--go_out="${GO_IMPORT}:." validate/validate.proto

tests/harness/go/harness.pb.go: bin/protoc-gen-go tests/harness/harness.proto
	# generates the test harness protos
	cd tests/harness && protoc -I . \
		--plugin=protoc-gen-go=$(shell pwd)/bin/protoc-gen-go \
		--go_out="module=${PACKAGE}/tests/harness/go,${GO_IMPORT}:./go" harness.proto

tests/harness/go/main/go-harness:
	# generates the go-specific test harness
	cd tests && go build -o ./harness/go/main/go-harness ./harness/go/main

tests/harness/cc/cc-harness: tests/harness/cc/harness.cc
	# generates the C++-specific test harness
	# use bazel which knows how to pull in the C++ common proto libraries
	bazel build //tests/harness/cc:cc-harness
	cp bazel-bin/tests/harness/cc/cc-harness $@
	chmod 0755 $@

tests/harness/java/java-harness:
	# generates the Java-specific test harness
	mvn -q -f java/pom.xml clean package -DskipTests

.PHONY: prepare-python-release
prepare-python-release:
	cp validate/validate.proto python/
	cp LICENSE python/

.PHONY: python-release
python-release: prepare-python-release
	rm -rf python/dist
	python3.9 -m build --no-isolation --sdist python
	# the below command should be identical to `python3.9 -m build --wheel`
	# however that returns mysterious `error: could not create 'build': File exists`.
	# setuptools copies source and data files to a temporary build directory,
	# but why there's a collision or why setuptools stopped respecting the `build_lib` flag is unclear.
	# As a workaround, we build a source distribution and then separately build a wheel from it.
	python3.9 -m pip wheel --wheel-dir python/dist --no-deps python/dist/*
	python3.9 -m twine upload --verbose --skip-existing --repository ${PYPI_REPO} --username "__token__" --password ${PGV_PYPI_TOKEN} python/dist/*

.PHONY: check-generated
check-generated: ## run during CI; this checks that the checked-in generated code matches the generated version.
	for f in validate/validate.pb.go ; do \
	  mv $$f $$f.original ; \
	  make $$f ; \
	  mv $$f $$f.generated ; \
	  cp $$f.original $$f ; \
	  diff $$f.original $$f.generated ; \
	done

.PHONY: ci
ci: lint bazel testcases bazel-tests build_generation_tests example-workspace check-generated

.PHONY: clean
clean: ## clean up generated files
	(which bazel && bazel clean) || true
	rm -f \
		bin/protoc-gen-go \
		bin/harness \
		tests/harness/cc/cc-harness \
		tests/harness/go/main/go-harness \
		tests/harness/go/harness.pb.go
	rm -rf \
		tests/harness/cases/go \
		tests/harness/cases/other_package/go \
		tests/harness/cases/yet_another_package/go
	rm -rf \
		python/dist \
		python/*.egg-info
