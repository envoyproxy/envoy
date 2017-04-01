# Building Envoy with Bazel

## Production environments

To build Envoy with Bazel in a production environment, where the [Envoy
dependencies](https://lyft.github.io/envoy/docs/install/requirements.html) are typically
independently sourced, the following steps should be followed:

1. [Install Bazel](https://bazel.build/versions/master/docs/install.html) in your environment.
2. Configure, build and/or install the [Envoy dependencies](https://lyft.github.io/envoy/docs/install/requirements.html).
3. Configure a Bazel [WORKSPACE](https://bazel.build/versions/master/docs/be/workspace.html)
   to point Bazel at the Envoy dependencies. An example is provided in the CI Docker image
   [WORKSPACE](https://github.com/lyft/envoy/blob/master/ci/WORKSPACE) and corresponding
   [BUILD](https://github.com/lyft/envoy/blob/master/ci/prebuilt/BUILD) files.
4. `bazel build --package_path %workspace%:<path to Envoy source tree> //source/exe:envoy-static` 
   from the directory containing your WORKSPACE.

## Quick start Bazel build for developers

As a developer convenience, a [WORKSPACE](https://github.com/lyft/envoy/blob/master/WORKSPACE) and
[rules for building a recent
version](https://github.com/lyft/envoy/blob/master/bazel/repositories.bzl) of the various Envoy
dependencies are provided. These are provided as is, they are only suitable for development and
testing purposes. The specific versions of the Envoy dependencies used in this build may not be
up-to-date with the latest security patches. In addition, Bazel build rules for deps have been
improvised in some cases and this build is not checked by CI.

1. [Install Bazel](https://bazel.build/versions/master/docs/install.html) in your environment.
2. `bazel build //source/exe:envoy-static` from the Envoy source directory.

## Building Bazel with the CI Docker image

Bazel can also be built with the Docker image used for CI, by installing Docker and executing:

```
./ci/run_envoy_docker.sh ./ci/do_ci.sh bazel.debug
```

See also the [documentation](https://github.com/lyft/envoy/tree/master/ci) for developer use of the
CI Docker image.

## Using a compiler toolchain in a non-standard location

By setting the `CC` and `LD_LIBRARY_PATH` in the environment that Bazel executes from as
appropriate, an arbitrary compiler toolchain and standard library location can be specified. One
slight caveat is that (at the time of writing), Bazel expects the binutils in `$(dirname $CC)` to be
unprefixed, e.g. `as` instead of `x86_64-linux-gnu-as`.

# Testing Envoy with Bazel

All the Envoy tests can be built and run with:

```
bazel test //test/...
```

An individual test target can be run with a more specific Bazel
[label](https://bazel.build/versions/master/docs/build-ref.html#Labels), e.g. to build and run only
the units tests in
[test/common/http/async_client_impl_test.cc](https://github.com/lyft/envoy/blob/master/test/common/http/async_client_impl_test.cc):

```
bazel test //test/commont/http:async_client_impl_test
```
