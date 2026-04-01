# Licensed under the Apache License, Version 2.0 (the "License")

# Plugin name.
name := protoc-gen-validate

# Root dir returns absolute path of current directory. It has a trailing "/".
root_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Local cache directory.
cache_dir := $(root_dir).cache

# Directory of Go tools.
go_tools_dir := $(cache_dir)/tools/go

# Directory of prepackaged tools (e.g. protoc).
prepackaged_tools_dir := $(cache_dir)/tools/prepackaged

# Currently we resolve Go using `which`. But more sophisticated approach is to use infer GOROOT.
go     := $(shell which go)
goarch := $(shell $(go) env GOARCH)
goexe  := $(shell $(go) env GOEXE)
goos   := $(shell $(go) env GOOS)

# The current binary location for the current runtime (goos, goarch). We install our plugin here.
current_binary_path := build/$(name)_$(goos)_$(goarch)
current_binary      := $(current_binary_path)/$(name)$(goexe)

# This makes sure protoc can access the installed plugins.
export PATH := $(root_dir)$(current_binary_path):$(go_tools_dir)/bin:$(prepackaged_tools_dir)/bin:$(PATH)

# The main generated file.
validate_pb_go := validate/validate.pb.go

# List of harness test cases.
tests_harness_cases := \
	/harness \
	/harness/cases \
	/harness/cases/other_package \
	/harness/cases/yet_another_package

# Include versions of tools we build on-demand
include Tools.mk
# This provides the "help" target.
include tools/build/Help.mk
# This sets some required environment variables.
include tools/build/Env.mk

# Path to the installed protocol buffer compiler.
protoc := $(prepackaged_tools_dir)/bin/protoc

# Go based tools.
bazel         := $(go_tools_dir)/bin/bazelisk
protoc-gen-go := $(go_tools_dir)/bin/protoc-gen-go

test: $(bazel) $(tests_harness_cases) ## Run tests
	@$(bazel) test //tests/... --test_output=errors

build: $(current_binary) ## Build the plugin

clean: ## Clean all build and test artifacts
	@rm -f $(validate_pb_go)
	@rm -f $(current_binary)

check: ## Verify contents of last commit
	@# Make sure the check-in is clean
	@if [ ! -z "`git status -s`" ]; then \
		echo "The following differences will fail CI until committed:"; \
		git diff --exit-code; \
	fi

# This provides shortcut to various bazel related targets.
sanity: bazel-build bazel-build-tests-generation bazel-test-example-workspace

bazel-build: $(bazel) ## Build the plugin using bazel
	@$(bazel) build //:$(name)
	@mkdir -p $(current_binary_path)
	@cp -f bazel-bin/$(name)_/$(name)$(goexe) $(current_binary)

bazel-build-tests-generation: $(bazel) ## Build tests generation using bazel
	@$(bazel) build //tests/generation/...

bazel-test-example-workspace: $(bazel) ## Test example workspace using bazel
	@cd example-workspace && bazel test //... --test_output=errors

# Generate validate/validate.pb.go from validate/validate.proto.
$(validate_pb_go): $(protoc) $(protoc-gen-go) validate/validate.proto
	@$(protoc) -I . --go_opt=paths=source_relative --go_out=. $(filter %.proto,$^)

# Build target for current binary.
build/$(name)_%/$(name)$(goexe): $(validate_pb_go)
	@GOBIN=$(root_dir)$(current_binary_path) $(go) install .

# Generate all required files for harness tests in Go.
$(tests_harness_cases): $(current_binary)
	$(call generate-test-cases-go,tests$@)

# Generates a test-case for Go.
define generate-test-cases-go
	@cd $1 && \
	mkdir -p go && \
	$(protoc) \
		-I . \
		-I $(root_dir) \
		--go_opt=paths=source_relative \
		--go_out=go \
		--validate_opt=paths=source_relative \
		--validate_out=lang=go:go \
		*.proto
endef

include tools/build/Installer.mk
