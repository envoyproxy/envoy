# Developer use of CI Docker images

Two flavors of Envoy Docker images, based on Ubuntu and Alpine Linux, are built.

## Ubuntu envoy image
The Ubuntu based Envoy Docker image at [`lyft/envoy-build:<hash>`](https://hub.docker.com/r/lyft/envoy-build/) is used for Travis CI checks,
where `<hash>` is specified in [ci_steps.sh](https://github.com/lyft/envoy/blob/master/ci/ci_steps.sh). Developers
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

# Building and running tests as a developer

An example basic invocation to build a developer version of the Envoy static binary (using the Bazel `fastbuild` type) is:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

The Envoy binary can be found in `/tmp/envoy-docker-build/envoy/source/exe/envoy-fastbuild` on the Docker host. You
can control this by setting `ENVOY_DOCKER_BUILD_DIR` in the environment, e.g. to
generate the binary in `~/build/envoy/source/exe/envoy-fastbuild` you can run:


```bash
ENVOY_DOCKER_BUILD_DIR=~/build ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev.server_only'
```

For a release version of the Envoy binary you can run:

```bash
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release.server_only'
```

The artifacts can be found in `/tmp/envoy-docker-build/envoy/source/exe/envoy` (or wherever
`$ENVOY_DOCKER_BUILD_DIR` points).


* `bazel.asan` &mdash; build and run tests under `-c dbg --config=asan`.
* `bazel.dev` &mdash; build Envoy static binary and run tests under `-c fastbuild`.
* `bazel.release` &mdash; build Envoy static binary and run tests under `-c opt`.
* `bazel.release.server_only` &mdash; build Envoy static binary under `-c opt`.
* `bazel.coverage` &mdash; build and run tests under `-c dbg`, generating coverage information in `<SOURCE_DIR>/generated/coverage/coverage.html`.
* `check_format`&mdash; run `clang-format` 3.6 and `buildifier` on entire source tree.
* `fix_format`&mdash; run and enforce `clang-format` 3.6 and `buildifier` on entire source tree.
