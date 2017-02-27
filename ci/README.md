# Developer use of CI Docker image

The Docker image at [`lyft/envoy-build:<hash>`](https://hub.docker.com/r/lyft/envoy-build/) is used for Travis CI checks, where `<hash>` is specified in
[ci_steps.sh](https://github.com/lyft/envoy/blob/master/ci/ci_steps.sh). Developers
may work with `lyft/envoy-build:latest` to provide a self-contained environment for
building Envoy binaries and running tests that reflects the latest built image. Moreover, the Docker
image at [`lyft/envoy:<hash>`](https://hub.docker.com/r/lyft/envoy/) is an image that has an Envoy binary at `usr/local/bin/envoy`. The `<hash>`
corresponds to the master commit at which the binary was compiled. Lastly, `lyft/envoy:latest` contains
an Envoy binary built from the latest tip of master that passed tests.

An example basic invocation to build a debug image and run all tests is:

```bash
docker pull lyft/envoy-build:latest && docker run -t -i -u $(id -u):$(id -g) -v <SOURCE_DIR>:/source lyft/envoy-build:latest /bin/bash -c "cd /source && ci/do_ci.sh debug"
```

On OSX using the command below may work better. Unlike on Linux, users are not
synced between the container and OS. Additionally, the bind mount will not
create artifacts with the same ownership as in the container ([read more about
osxfs][osxfs]).

```bash
docker pull lyft/envoy-build:latest && docker run -t -i -u root:root -v <SOURCE_DIR>:/source lyft/envoy-build:latest /bin/bash -c "cd /source && ci/do_ci.sh debug"
```

This bind mounts `<SOURCE_DIR>`, which allows for changes on the local
filesystem to be consumed and outputs build artifacts in `<SOURCE_DIR>/build`.
The static Envoy binary can be found in `<SOURCE_DIR>/build/source/exe/envoy`.

The `do_ci.sh` targets are:

* `asan` &mdash; build and run tests with [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer).
* `coverage` &mdash; build and run tests, generating coverage information in `<SOURCE_DIR>/build/coverage.html`.
* `debug` &mdash; build debug binary and run tests.
* `docs` &mdash; build documentation, generated docs are found in `<SOURCE_DIR>/generated`.
* `fix_format`&mdash; run `clang-format` 3.6 on entire source tree.
* `normal` &mdash; build unstripped optimized binary and run tests .
* `server_only` &mdash; build stripped optimized binary only.

A convenient shell function to define is:

```bash
run_envoy_docker() { docker pull lyft/envoy-build:latest && docker run -t -i -u $(id -u):$(id -g) -v $PWD:/source lyft/envoy-build:latest /bin/bash -c "cd /source && $*";}
```

Or on OSX.

```bash
run_envoy_docker() { docker pull lyft/envoy-build:latest && docker run -t -i -u root:root -v $PWD:/source lyft/envoy-build:latest /bin/bash -c "cd /source && $*";}
```

This then allows for a simple invocation of `run_envoy_docker './ci/do_ci.sh debug'` from the
Envoy source tree.

## Advanced developer features

* Any parameters following the `do_ci.sh` target are passed in as command-line
  arguments to the `envoy-test` binary during unit test execution. This allows
  for [GTest](https://github.com/google/googletest) flags to be passed, e.g.
  `run_envoy_docker './ci/do_ci.sh debug --gtest_filter="*Dns*"'`.

* A `UNIT_TEST_ONLY` environment variable is available to control test execution to limit testing to
  just unit tests, e.g. `run_envoy_docker 'UNIT_TEST_ONLY=1 ./ci/do_ci.sh debug --gtest_filter="*Dns*"'`.

* A `RUN_TEST_UNDER` environment variable is available to specify an executable to run the tests
  under. For example, to run a subset of tests under `gdb`: `run_envoy_docker 'RUN_TEST_UNDER="gdb --args" UNIT_TEST_ONLY=1 ./ci/do_ci.sh debug --gtest_filter="*Dns*"'`.

* A `SKIP_CHECK_FORMAT` environment variable is available to skip `clang-format` checks while developing locally, e.g. `run_envoy_docker 'SKIP_CHECK_FORMAT=1 ./ci/do_ci.sh debug'`.


[osxfs]: https://docs.docker.com/docker-for-mac/osxfs/
