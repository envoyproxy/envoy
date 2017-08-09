# Developer use of CI Docker images

Two flavors of Envoy Docker images, based on Ubuntu and Alpine Linux, are built.

## Ubuntu envoy image
The Ubuntu based Envoy Docker image at [`lyft/envoy-build:<hash>`](https://hub.docker.com/r/lyft/envoy-build/) is used for Travis CI checks,
where `<hash>` is specified in [`envoy_build_sha.sh`](https://github.com/lyft/envoy/blob/master/ci/envoy_build_sha.sh). Developers
may work with `lyft/envoy-build:latest` to provide a self-contained environment for building Envoy binaries and
running tests that reflects the latest built Ubuntu Envoy image. Moreover, the Docker image
at [`lyft/envoy:<hash>`](https://hub.docker.com/r/lyft/envoy/) is an image that has an Envoy binary at `/usr/local/bin/envoy`. The `<hash>`
corresponds to the master commit at which the binary was compiled. Lastly, `lyft/envoy:latest` contains an Envoy
binary built from the latest tip of master that passed tests.

## Alpine envoy image

Minimal images based on Alpine Linux allow for quicker deployment of Envoy. Two Alpine based images are built,
one with an Envoy binary with debug (`lyft/envoy-alpine-debug`) symbols and one stripped of them (`lyft/envoy-alpine`).
Both images are pushed with two different tags: `<hash>` and `latest`. Parallel to the Ubuntu images above, `<hash>` corresponds to the
master commit at which the binary was compiled, and `latest` corresponds to a binary built from the latest tip of master that passed tests.

# Build image base and compiler versions

The current build image is based on Ubuntu 16.04 (Xenial) which uses the GCC 5.4 compiler. We also
install and use the clang-5.0 compiler for some sanitizing runs.

# Building and running tests as a developer

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

The `./ci/run_envoy_docker.sh './ci/do_ci.sh <TARGET>'` targets are:


* `bazel.asan` &mdash; build and run tests under `-c dbg --config=clang-asan` with clang-5.0.
* `bazel.debug` &mdash; build Envoy static binary and run tests under `-c dbg`.
* `bazel.debug.server_only` &mdash; build Envoy static binary under `-c dbg`.
* `bazel.dev` &mdash; build Envoy static binary and run tests under `-c fastbuild` with gcc.
* `bazel.release` &mdash; build Envoy static binary and run tests under `-c opt` with gcc.
* `bazel.release.server_only` &mdash; build Envoy static binary under `-c opt` with gcc.
* `bazel.coverage` &mdash; build and run tests under `-c dbg` with gcc, generating coverage information in `$ENVOY_DOCKER_BUILD_DIR/envoy/generated/coverage/coverage.html`.
* `bazel.tsan` &mdash; build and run tests under `-c dbg --config=clang-tsan` with clang-5.0.
* `check_format`&mdash; run `clang-format` 5.0 and `buildifier` on entire source tree.
* `fix_format`&mdash; run and enforce `clang-format` 5.0 and `buildifier` on entire source tree.

# Testing changes to the build image as a developer

While all changes to the build image should eventually be upstreamed, it can be useful to
test those changes locally before sending out a pull request.  To experiment
with a local clone of the upstream build image you can make changes to files such as
build_container.sh locally and then run:

```bash
cd ci/build_container
TRAVIS_COMMIT=my_tag ./docker_build.sh  # Wait patiently for quite some time
cd ../..
IMAGE_ID=my_tag ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.whatever'
```

The final call will run against your local copy of the build image.

