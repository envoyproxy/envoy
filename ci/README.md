# Developer use of CI Docker images

Two flavors of Envoy Docker images, based on Ubuntu and Alpine Linux, are built.

## Ubuntu Envoy image
The Ubuntu based Envoy Docker image at [`envoyproxy/envoy-build:<hash>`](https://hub.docker.com/r/envoyproxy/envoy-build/) is used for CircleCI checks,
where `<hash>` is specified in [`envoy_build_sha.sh`](https://github.com/envoyproxy/envoy/blob/master/ci/envoy_build_sha.sh). Developers
may work with `envoyproxy/envoy-build:latest` to provide a self-contained environment for building Envoy binaries and
running tests that reflects the latest built Ubuntu Envoy image. Moreover, the Docker image
at [`envoyproxy/envoy:<hash>`](https://hub.docker.com/r/envoyproxy/envoy/) is an image that has an Envoy binary at `/usr/local/bin/envoy`. The `<hash>`
corresponds to the master commit at which the binary was compiled. Lastly, `envoyproxy/envoy-dev:latest` contains an Envoy
binary built from the latest tip of master that passed tests.

## Alpine Envoy image

Minimal images based on Alpine Linux allow for quicker deployment of Envoy. Two Alpine based images are built,
one with an Envoy binary with debug (`envoyproxy/envoy-alpine-debug`) symbols and one stripped of them (`envoyproxy/envoy-alpine`).
Both images are pushed with two different tags: `<hash>` and `latest`. Parallel to the Ubuntu images above, `<hash>` corresponds to the
master commit at which the binary was compiled, and `latest` corresponds to a binary built from the latest tip of master that passed tests.

# Build image base and compiler versions

Currently there are three build images:

* `envoyproxy/envoy-build` &mdash; alias to `envoyproxy/envoy-build-ubuntu`.
* `envoyproxy/envoy-build-ubuntu` &mdash; based on Ubuntu 16.04 (Xenial) with GCC 7 and Clang 9 compiler.
* `envoyproxy/envoy-build-centos` &mdash; based on CentOS 7 with GCC 7 and Clang 9 compiler, this image is experimental and not well tested.

The source for these images is located in the [envoyproxy/envoy-build-tools](https://github.com/envoyproxy/envoy-build-tools)
repository.

We use the Clang compiler for all CI runs with tests. We have an additional CI run with GCC which builds binary only.

# C++ standard library

As of November 2019 after [#8859](https://github.com/envoyproxy/envoy/pull/8859) the official released binary is
[linked against libc++ on Linux](https://github.com/envoyproxy/envoy/blob/master/bazel/README.md#linking-against-libc-on-linux).
To override the C++ standard library in your build, set environment variable `ENVOY_STDLIB` to `libstdc++` or `libc++` and
run `./ci/do_ci.sh` as described below.

# Building and running tests as a developer

An example basic invocation to build a developer version of the Envoy static binary (using the Bazel `fastbuild` type) is:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

The build image defaults to `envoyproxy/envoy-build-ubuntu`, but you can choose build image by setting `IMAGE_NAME` in the environment.

In case your setup is behind a proxy, set `http_proxy` and `https_proxy` to the proxy servers before invoking the build.

```bash
IMAGE_NAME=envoyproxy/envoy-build-ubuntu http_proxy=http://proxy.foo.com:8080 https_proxy=http://proxy.bar.com:8080 ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

The Envoy binary can be found in `/tmp/envoy-docker-build/envoy/source/exe/envoy-fastbuild` on the Docker host. You
can control this by setting `ENVOY_DOCKER_BUILD_DIR` in the environment, e.g. to
generate the binary in `~/build/envoy/source/exe/envoy-fastbuild` you can run:

```bash
ENVOY_DOCKER_BUILD_DIR=~/build ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

For a release version of the Envoy binary you can run:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release.server_only'
```

The build artifact can be found in `/tmp/envoy-docker-build/envoy/source/exe/envoy` (or wherever
`$ENVOY_DOCKER_BUILD_DIR` points).

For a debug version of the Envoy binary you can run:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.debug.server_only'
```

The build artifact can be found in `/tmp/envoy-docker-build/envoy/source/exe/envoy-debug` (or wherever
`$ENVOY_DOCKER_BUILD_DIR` points).

To leverage a [bazel remote cache](https://github.com/envoyproxy/envoy/tree/master/bazel#advanced-caching-setup) add the http_remote_cache endpoint to
the BAZEL_BUILD_EXTRA_OPTIONS environment variable

```bash
./ci/run_envoy_docker.sh "BAZEL_BUILD_EXTRA_OPTIONS='--remote_http_cache=http://127.0.0.1:28080' ./ci/do_ci.sh bazel.release"
```

The `./ci/run_envoy_docker.sh './ci/do_ci.sh <TARGET>'` targets are:

* `bazel.api` &mdash; build and run API tests under `-c fastbuild` with clang.
* `bazel.asan` &mdash; build and run tests under `-c dbg --config=clang-asan` with clang.
* `bazel.asan <test>` &mdash; build and run a specified test or test dir under `-c dbg --config=clang-asan` with clang.
* `bazel.debug` &mdash; build Envoy static binary and run tests under `-c dbg`.
* `bazel.debug <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c dbg`.
* `bazel.debug.server_only` &mdash; build Envoy static binary under `-c dbg`.
* `bazel.dev` &mdash; build Envoy static binary and run tests under `-c fastbuild` with clang.
* `bazel.dev <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c fastbuild` with clang.
* `bazel.release` &mdash; build Envoy static binary and run tests under `-c opt` with clang.
* `bazel.release <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c opt` with clang.
* `bazel.release.server_only` &mdash; build Envoy static binary under `-c opt` with clang.
* `bazel.sizeopt` &mdash; build Envoy static binary and run tests under `-c opt --config=sizeopt` with clang.
* `bazel.sizeopt <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c opt --config=sizeopt` with clang.
* `bazel.sizeopt.server_only` &mdash; build Envoy static binary under `-c opt --config=sizeopt` with clang.
* `bazel.coverage` &mdash; build and run tests under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`.
* `bazel.coverage <test>` &mdash; build and run a specified test or test dir under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`.
* `bazel.coverity` &mdash; build Envoy static binary and run Coverity Scan static analysis.
* `bazel.msan` &mdash; build and run tests under `-c dbg --config=clang-msan` with clang.
* `bazel.msan <test>` &mdash; build and run a specified test or test dir under `-c dbg --config=clang-msan` with clang.
* `bazel.tsan` &mdash; build and run tests under `-c dbg --config=clang-tsan` with clang.
* `bazel.tsan <test>` &mdash; build and run a specified test or test dir under `-c dbg --config=clang-tsan` with clang.
* `bazel.fuzz` &mdash; build and run fuzz tests under `-c dbg --config=asan-fuzzer` with clang.
* `bazel.fuzz <test>` &mdash; build and run a specified fuzz test or test dir under `-c dbg --config=asan-fuzzer` with clang. If specifying a single fuzz test, must use the full target name with "_with_libfuzzer" for `<test>`.
* `bazel.compile_time_options` &mdash; build Envoy and run tests with various compile-time options toggled to their non-default state, to ensure they still build.
* `bazel.compile_time_options <test>` &mdash; build Envoy and run a specified test or test dir with various compile-time options toggled to their non-default state, to ensure they still build.
* `bazel.clang_tidy` &mdash; build and run clang-tidy over all source files.
* `check_format`&mdash; run `clang-format` and `buildifier` on entire source tree.
* `fix_format`&mdash; run and enforce `clang-format` and `buildifier` on entire source tree.
* `check_spelling`&mdash; run `misspell` on entire project.
* `fix_spelling`&mdash; run and enforce `misspell` on entire project.
* `check_spelling_pedantic`&mdash; run `aspell` on C++ and proto comments.
* `docs`&mdash; build documentation tree in `generated/docs`.

# Testing changes to the build image as a developer

While all changes to the build image should eventually be upstreamed, it can be useful to
test those changes locally before sending out a pull request. To experiment
with a local clone of the upstream build image you can make changes to files such as
build_container.sh locally and then run:

```bash
DISTRO=ubuntu
cd ci/build_container
LINUX_DISTRO="${DISTRO}" CIRCLE_SHA1=my_tag ./docker_build.sh  # Wait patiently for quite some time
cd ../..
IMAGE_NAME="envoyproxy/envoy-build-${DISTRO}" IMAGE_ID=my_tag ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.whatever'
```

This build the Ubuntu based `envoyproxy/envoy-build-ubuntu` image, and the final call will run against your local copy of the build image.

# macOS Build Flow

The macOS CI build is part of the [CircleCI](https://circleci.com/gh/envoyproxy/envoy) workflow.
Dependencies are installed by the `ci/mac_ci_setup.sh` script, via [Homebrew](https://brew.sh),
which is pre-installed on the CircleCI macOS image. The dependencies are cached are re-installed
on every build. The `ci/mac_ci_steps.sh` script executes the specific commands that
build and test Envoy. If Envoy cannot be built (`error: /Library/Developer/CommandLineTools/usr/bin/libtool: no output file specified (specify with -o output)`),
ensure that Xcode is installed.

# Coverity Scan Build Flow

[Coverity Scan Envoy Project](https://scan.coverity.com/projects/envoy-proxy)

Coverity Scan static analysis is not run within Envoy CI. However, Envoy can be locally built and
submitted for analysis. A Coverity Scan Envoy project token must be generated from the
[Coverity Project Settings](https://scan.coverity.com/projects/envoy-proxy?tab=project_settings).
Only a Coverity Project Administrator can create a token. With this token, running
`ci/do_coverity_local.sh` will use the Ubuntu based `envoyproxy/envoy-build-ubuntu` image to build the
Envoy static binary with the Coverity Scan tool chain. This process generates an artifact,
envoy-coverity-output.tgz, that is uploaded to Coverity for static analysis.

To build and submit for analysis:
```bash
COVERITY_TOKEN={generated Coverity project token} ./ci/do_coverity_local.sh
```
