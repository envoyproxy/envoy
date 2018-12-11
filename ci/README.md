# Developer use of CI Docker images

Two flavors of Envoy Docker images, based on Ubuntu and Alpine Linux, are built.

## Ubuntu envoy image
The Ubuntu based Envoy Docker image at [`envoyproxy/envoy-build:<hash>`](https://hub.docker.com/r/envoyproxy/envoy-build/) is used for CircleCI checks,
where `<hash>` is specified in [`envoy_build_sha.sh`](https://github.com/envoyproxy/envoy/blob/master/ci/envoy_build_sha.sh). Developers
may work with `envoyproxy/envoy-build:latest` to provide a self-contained environment for building Envoy binaries and
running tests that reflects the latest built Ubuntu Envoy image. Moreover, the Docker image
at [`envoyproxy/envoy:<hash>`](https://hub.docker.com/r/envoyproxy/envoy/) is an image that has an Envoy binary at `/usr/local/bin/envoy`. The `<hash>`
corresponds to the master commit at which the binary was compiled. Lastly, `envoyproxy/envoy:latest` contains an Envoy
binary built from the latest tip of master that passed tests.

## Alpine envoy image

Minimal images based on Alpine Linux allow for quicker deployment of Envoy. Two Alpine based images are built,
one with an Envoy binary with debug (`envoyproxy/envoy-alpine-debug`) symbols and one stripped of them (`envoyproxy/envoy-alpine`).
Both images are pushed with two different tags: `<hash>` and `latest`. Parallel to the Ubuntu images above, `<hash>` corresponds to the
master commit at which the binary was compiled, and `latest` corresponds to a binary built from the latest tip of master that passed tests.

# Build image base and compiler versions

Currently there are three build images:

* `envoyproxy/envoy-build` &mdash; alias to `envoyproxy/envoy-build-ubuntu`.
* `envoyproxy/envoy-build-ubuntu` &mdash; based on Ubuntu 16.04 (Xenial) which uses the GCC 5.4 compiler.
* `envoyproxy/envoy-build-centos` &mdash; based on CentOS 7 which uses the GCC 5.3.1 compiler (devtoolset-4).

For Ubuntu, we also install and use the clang-7 compiler for some sanitizing runs.

On CentOS 7, we install the clang-5 compiler. However, the sanitizing runs have not been tested there!

# Building and running tests as a developer

An example basic invocation to build a developer version of the Envoy static binary (using the Bazel `fastbuild` type) is:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

The build image defaults to `envoyproxy/envoy-build-ubuntu`, but you can choose build image by setting `IMAGE_NAME` in the environment.
```

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
`$ENVOY_DOCKER_BUILD_DIR` points) and the same binary is in `<PROJECT_ROOT>/build_release`. In addition, a stripped version has been created in `<PROJECT_ROOT>/build_release_stripped`.

For a debug version of the Envoy binary you can run:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.debug.server_only'
```

The build artifact can be found in `/tmp/envoy-docker-build/envoy/source/exe/envoy-debug` (or wherever
`$ENVOY_DOCKER_BUILD_DIR` points).

The `./ci/run_envoy_docker.sh './ci/do_ci.sh <TARGET>'` targets are:

* `bazel.api` &mdash; build and run API tests under `-c fastbuild` with clang.
* `bazel.asan` &mdash; build and run tests under `-c dbg --config=clang-asan` with clang.
* `bazel.debug` &mdash; build Envoy static binary and run tests under `-c dbg`.
* `bazel.debug.server_only` &mdash; build Envoy static binary under `-c dbg`.
* `bazel.dev` &mdash; build Envoy static binary and run tests under `-c fastbuild` with clang.
* `bazel.release` &mdash; build Envoy static binary and run tests under `-c opt` with gcc.
* `bazel.release <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c opt` with gcc.
* `bazel.release.server_only` &mdash; build Envoy static binary under `-c opt` with gcc.
* `bazel.coverage` &mdash; build and run tests under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`.
* `bazel.coverity` &mdash; build Envoy static binary and run Coverity Scan static analysis.
* `bazel.tsan` &mdash; build and run tests under `-c dbg --config=clang-tsan` with clang.
* `bazel.clang_tidy` &mdash; build and run clang-tidy over all source files.
* `check_format`&mdash; run `clang-format-6.0` and `buildifier` on entire source tree.
* `fix_format`&mdash; run and enforce `clang-format-6.0` and `buildifier` on entire source tree.
* `check_spelling`&mdash; run `misspell` on entire project.
* `fix_spelling`&mdash; run and enforce `misspell` on entire project.
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

# Building a CentOS 7 Binary

Currently, there is no binary image for CentOS 7 available on Docker Hub. However, a build image based on CentOS 7 has been created and pushed again so you can create your own binary for CentOS 7.

Here are some instructions on how to do that locally.

## Pre-Requisites

For a sufficiently fast build, you should have access to a computer or cloud VM with a minimum of 8GB RAM and 4 CPUs/Cores running a later Linux (CentOS 7, Ubuntu 16.04 or 18.04) with Docker CE installed. The build image requres at least Docker 17.03.0-ce. The latest build image has been tested on Docker CE 18.09.0.

Please refer to the Docker documentation on how to install and setup Docker on your specific OS.

## Building

Once you have cloned this repository onto your build machine, you can run the following command to build a binary for CentOS 7:

`IMAGE_NAME=envoyproxy/envoy-build-centos IMAGE_ID="latest" ./ci/run_envoy_docker.sh 'BAZEL_BUILD_EXTRA_OPTIONS=--copt=-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1 ./ci/do_ci.sh bazel.release.server_only'`

This will pull the latest CentOS build image from Docker Hub.

In case you want to build a binary from a specific tag, you will need to get the git commit hash (the full length hash and not just the first 7 characters) and replace the `latest` with this hash string in the `IMAGE_ID` above.

Please note the important 

`BAZEL_BUILD_EXTRA_OPTIONS=--copt=-DENVOY_IGNORE_GLIBCXX_USE_CXX11_ABI_ERROR=1`

passed into the build script!

If you don't add this, your build will fail due to a incompatibility between GCC 5.3 on CentOS and the code developed and built under GCC 5.4. This option will have to be used until both Ubuntu and CentOS have been synchronized on the same build toolchain.

**One last note**: the build image has only been tested with the `bazel.release.server_only` target. All other targets, especially those using `clang` are not guaranteed to work on CentOS since the only purpose of this image is to produce an `envoy` binary for CentOS 7.

# MacOS Build Flow

The MacOS CI build is part of the [CircleCI](https://circleci.com/gh/envoyproxy/envoy) workflow.
Dependencies are installed by the `ci/mac_ci_setup.sh` script, via [Homebrew](https://brew.sh),
which is pre-installed on the CircleCI MacOS image. The dependencies are cached are re-installed
on every build. The `ci/mac_ci_steps.sh` script executes the specific commands that
build and test Envoy.

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
