# Developer use of CI Docker images

There are two available flavors of Envoy Docker images for Linux, based on Ubuntu and Alpine Linux
and an image based on Windows2019.

## Ubuntu Envoy image

The Ubuntu based Envoy Docker image at [`envoyproxy/envoy-build:<hash>`](https://hub.docker.com/r/envoyproxy/envoy-build/) is used for CI checks,
where `<hash>` is specified in [`envoy_build_sha.sh`](https://github.com/envoyproxy/envoy/blob/main/ci/envoy_build_sha.sh). Developers
may work with the latest build image SHA in [envoy-build-tools](https://github.com/envoyproxy/envoy-build-tools/blob/main/toolchains/rbe_toolchains_config.bzl#L8)
repo to provide a self-contained environment for building Envoy binaries and running tests that reflects the latest built Ubuntu Envoy image.
Moreover, the Docker image at [`envoyproxy/envoy-dev:<hash>`](https://hub.docker.com/r/envoyproxy/envoy-dev/) is an image that has an Envoy binary at `/usr/local/bin/envoy`.
The `<hash>` corresponds to the main commit at which the binary was compiled. Lastly, `envoyproxy/envoy-dev:latest` contains an Envoy
binary built from the latest tip of main that passed tests.

## Alpine Envoy image

Minimal images based on Alpine Linux allow for quicker deployment of Envoy. The Alpine base image is only built with symbols stripped.
To get the binary with symbols, use the corresponding Ubuntu based debug image. The image is pushed with two different tags:
`<hash>` and `latest`. Parallel to the Ubuntu images above, `<hash>` corresponds to the
main commit at which the binary was compiled, and `latest` corresponds to a binary built from the latest tip of main that passed tests.

## Windows 2019 Envoy image

The Windows 2019 based Envoy Docker image at [`envoyproxy/envoy-build-windows2019:<hash>`](https://hub.docker.com/r/envoyproxy/envoy-build-windows2019/)
is used for CI checks, where `<hash>` is specified in [`envoy_build_sha.sh`](https://github.com/envoyproxy/envoy/blob/main/ci/envoy_build_sha.sh).
Developers may work with the most recent `envoyproxy/envoy-build-windows2019` image to provide a self-contained environment for building Envoy binaries and
running tests that reflects the latest built Windows 2019 Envoy image.

# Build image base and compiler versions

Currently there are three build images for Linux and one for Windows:

* `envoyproxy/envoy-build` &mdash; alias to `envoyproxy/envoy-build-ubuntu`.
* `envoyproxy/envoy-build-ubuntu` &mdash; based on Ubuntu 20.04 (Focal) with GCC 9 and Clang 14 compiler.
* `envoyproxy/envoy-build-centos` &mdash; based on CentOS 7 with GCC 9 and Clang 14 compiler, this image is experimental and not well tested.
* `envoyproxy/envoy-build-windows2019` &mdash; based on Windows ltsc2019 with VS 2019 Build Tools, as well as LLVM.

The source for these images is located in the [envoyproxy/envoy-build-tools](https://github.com/envoyproxy/envoy-build-tools)
repository.

We use the Clang compiler for all Linux CI runs with tests. We have an additional Linux CI run with GCC which builds binary only.

# C++ standard library

As of November 2019 after [#8859](https://github.com/envoyproxy/envoy/pull/8859) the official released binary is
[linked against libc++ on Linux](https://github.com/envoyproxy/envoy/blob/main/bazel/README.md#linking-against-libc-on-linux).
To override the C++ standard library in your build, set environment variable `ENVOY_STDLIB` to `libstdc++` or `libc++` and
run `./ci/do_ci.sh` as described below.

# Building and running tests as a developer

The `./ci/run_envoy_docker.sh` script can be used to set up a Docker container on Linux and Windows
to build an Envoy static binary and run tests.

The build image defaults to `envoyproxy/envoy-build-ubuntu` on Linux and
`envoyproxy/envoy-build-windows2019` on Windows, but you can choose build image by setting
`IMAGE_NAME` in the environment.

In case your setup is behind a proxy, set `http_proxy` and `https_proxy` to the proxy servers before
invoking the build.

```bash
IMAGE_NAME=envoyproxy/envoy-build-ubuntu http_proxy=http://proxy.foo.com:8080 https_proxy=http://proxy.bar.com:8080 ./ci/run_envoy_docker.sh <build_script_args>
```

Besides `http_proxy` and `https_proxy`, maybe you need to set `go_proxy` to replace the default GOPROXY in China.

```bash
IMAGE_NAME=envoyproxy/envoy-build-ubuntu go_proxy=https://goproxy.cn,direct http_proxy=http://proxy.foo.com:8080 https_proxy=http://proxy.bar.com:8080 ./ci/run_envoy_docker.sh <build_script_args>
```

## On Linux

An example basic invocation to build a developer version of the Envoy static binary (using the Bazel `fastbuild` type) is:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
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

To leverage a [bazel remote cache](https://github.com/envoyproxy/envoy/tree/main/bazel#advanced-caching-setup) add the remote cache endpoint to
the BAZEL_BUILD_EXTRA_OPTIONS environment variable

```bash
./ci/run_envoy_docker.sh "BAZEL_BUILD_EXTRA_OPTIONS='--remote_cache=http://127.0.0.1:28080' ./ci/do_ci.sh bazel.release"
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
* `bazel.dev.contrib` &mdash; build Envoy static binary with contrib and run tests under `-c fastbuild` with clang.
* `bazel.dev.contrib <test>` &mdash; build Envoy static binary with contrib and run a specified test or test dir under `-c fastbuild` with clang.
* `bazel.release` &mdash; build Envoy static binary and run tests under `-c opt` with clang.
* `bazel.release <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c opt` with clang.
* `bazel.release.server_only` &mdash; build Envoy static binary under `-c opt` with clang.
* `bazel.sizeopt` &mdash; build Envoy static binary and run tests under `-c opt --config=sizeopt` with clang.
* `bazel.sizeopt <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c opt --config=sizeopt` with clang.
* `bazel.sizeopt.server_only` &mdash; build Envoy static binary under `-c opt --config=sizeopt` with clang.
* `bazel.coverage` &mdash; build and run tests under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`.
* `bazel.coverage <test>` &mdash; build and run a specified test or test dir under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`. Specify `//contrib/...` to get contrib coverage.
* `bazel.msan` &mdash; build and run tests under `-c dbg --config=clang-msan` with clang.
* `bazel.msan <test>` &mdash; build and run a specified test or test dir under `-c dbg --config=clang-msan` with clang.
* `bazel.tsan` &mdash; build and run tests under `-c dbg --config=clang-tsan` with clang.
* `bazel.tsan <test>` &mdash; build and run a specified test or test dir under `-c dbg --config=clang-tsan` with clang.
* `bazel.fuzz` &mdash; build and run fuzz tests under `-c dbg --config=asan-fuzzer` with clang.
* `bazel.fuzz <test>` &mdash; build and run a specified fuzz test or test dir under `-c dbg --config=asan-fuzzer` with clang. If specifying a single fuzz test, must use the full target name with "_with_libfuzzer" for `<test>`.
* `bazel.compile_time_options` &mdash; build Envoy and run tests with various compile-time options toggled to their non-default state, to ensure they still build.
* `bazel.compile_time_options <test>` &mdash; build Envoy and run a specified test or test dir with various compile-time options toggled to their non-default state, to ensure they still build.
* `bazel.clang_tidy <files>` &mdash; build and run clang-tidy specified source files, if no files specified, runs against the diff with the last GitHub commit.
* `check_proto_format`&mdash; check configuration, formatting and build issues in API proto files.
* `fix_proto_format`&mdash; fix configuration, formatting and build issues in API proto files.
* `format`&mdash; run validation, linting and formatting tools.
* `docs`&mdash; build documentation tree in `generated/docs`.

## On Windows

An example basic invocation to build the Envoy static binary and run tests is:

```bash
./ci/run_envoy_docker.sh './ci/windows_ci_steps.sh'
```

You can pass additional command line arguments to `./ci/windows_ci_steps.sh` to list specific `bazel` arguments and build/test targets.
as set environment variables to adjust your container build environment as described above.

The Envoy binary can be found in `${TEMP}\envoy-docker-build\envoy\source\exe` on the Docker host. You
can control this by setting `ENVOY_DOCKER_BUILD_DIR` in the environment, e.g. to
generate the binary in `C:\Users\foo\build\envoy\source\exe` you can run:

```bash
ENVOY_DOCKER_BUILD_DIR="C:\Users\foo\build" ./ci/run_envoy_docker.sh './ci/windows_ci_steps.sh'
```

Note the quotations around the `ENVOY_DOCKER_BUILD_DIR` value to preserve the backslashes in the
path.

If you would like to run an interactive session to keep the build container running (to persist your local build environment), run:

```bash
./ci/run_envoy_docker.sh 'bash'
```

From an interactive session, you can invoke `bazel` directly, or use the `./ci/windows_ci_steps.sh` script to build and run tests.
Bazel will look for .bazelrc in the `${HOME}` path, which is mapped to the persistent path `${TEMP}\envoy-docker-build\` on the
Docker host.

# Testing changes to the build image as a developer

The base build image used in the CI flows here lives in the [envoy-build-tools](https://github.com/envoyproxy/envoy-build-tools)
repository. If you need to make and/or test changes to the build image, instructions to do so can be found in
the [build_container](https://github.com/envoyproxy/envoy-build-tools/blob/main/build_container/README.md) folder.
See the Dockerfiles and build scripts there for building a new image.

# macOS Build Flow

The macOS CI build is part of the [Azure Pipelines](https://dev.azure.com/cncf/envoy/_build) workflow.
Dependencies are installed by the `ci/mac_ci_setup.sh` script, via [Homebrew](https://brew.sh),
which is pre-installed on the [Azure Pipelines macOS image](https://github.com/actions/virtual-environments/blob/main/images/macos/macos-10.15-Readme.md).
The dependencies are cached and re-installed on every build. The `ci/mac_ci_steps.sh` script executes the specific commands that
build and test Envoy. Note that the full version of Xcode (not just Command Line Tools) is required.
