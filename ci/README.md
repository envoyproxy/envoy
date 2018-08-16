# Developer use of CI Docker images

Two flavors of Envoy Docker images, based on Ubuntu and Alpine Linux, are built.

## Ubuntu envoy image
The Ubuntu based Envoy Docker image at [`envoyproxy/envoy-build:<hash>`](https://hub.docker.com/r/envoyproxy/envoy-build/) is used for Travis CI checks,
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

We also install and use the clang-6.0 compiler for some sanitizing runs.

# Building and running tests as a developer

An example basic invocation to build a developer version of the Envoy static binary (using the Bazel `fastbuild` type) is:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

The build image defaults to `envoyproxy/envoy-build-ubuntu`, but you can choose build image by setting `IMAGE_NAME` in the environment,
e.g. to use the `envoyproxy/envoy-build-centos` image you can run:

```bash
IMAGE_NAME=envoyproxy/envoy-build-centos ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

In case your setup is behind a proxy, set `http_proxy` and `https_proxy` to the proxy servers before invoking the build.

```bash
IMAGE_NAME=envoyproxy/envoy-build-centos http_proxy=http://proxy.foo.com:8080 https_proxy=http://proxy.bar.com:8080 ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
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

The `./ci/run_envoy_docker.sh './ci/do_ci.sh <TARGET>'` targets are:

* `bazel.api` &mdash; build and run API tests under `-c fastbuild` with clang-6.0.
* `bazel.asan` &mdash; build and run tests under `-c dbg --config=clang-asan` with clang-6.0.
* `bazel.debug` &mdash; build Envoy static binary and run tests under `-c dbg`.
* `bazel.debug.server_only` &mdash; build Envoy static binary under `-c dbg`.
* `bazel.dev` &mdash; build Envoy static binary and run tests under `-c fastbuild` with gcc.
* `bazel.release` &mdash; build Envoy static binary and run tests under `-c opt` with gcc.
* `bazel.release <test>` &mdash; build Envoy static binary and run a specified test or test dir under `-c opt` with gcc.
* `bazel.release.server_only` &mdash; build Envoy static binary under `-c opt` with gcc.
* `bazel.coverage` &mdash; build and run tests under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`.
* `bazel.coverity` &mdash; build Envoy static binary and run Coverity Scan static analysis.
* `bazel.tsan` &mdash; build and run tests under `-c dbg --config=clang-tsan` with clang-6.0.
* `check_format`&mdash; run `clang-format-6.0` and `buildifier` on entire source tree.
* `fix_format`&mdash; run and enforce `clang-format-6.0` and `buildifier` on entire source tree.
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
To build the CentOS based `envoyproxy/envoy-build-ubuntu-centos` image, change `DISTRO` above to *centos*.

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
