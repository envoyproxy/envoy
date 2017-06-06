# Developer documentation

Envoy is built using the Bazel build system. Travis CI builds, tests, and runs coverage against all pull requests and the master branch.

To get started building Envoy locally, see the [Bazel quick start](https://github.com/lyft/envoy/blob/master/bazel/README.md#quick-start-bazel-build-for-developers). To run tests, there are Bazel [targets](https://github.com/lyft/envoy/blob/master/bazel/README.md#testing-envoy-with-bazel) for Google Test. To generate a coverage report, use the tooling for [gcovr](https://github.com/lyft/envoy/blob/master/bazel/README.md#coverage-builds).

Below is a list of additional documentation to aid the development process:

- [General build and installation documentation](https://lyft.github.io/envoy/docs/install/install.html)

- [Building and testing Envoy with Bazel](https://github.com/lyft/envoy/blob/master/bazel/README.md)

- [Managing external dependencies with Bazel](https://github.com/lyft/envoy/blob/master/bazel/EXTERNAL_DEPS.md)

- [Guide to Envoy Bazel rules (managing `BUILD` files)](https://github.com/lyft/envoy/blob/master/bazel/DEVELOPER.md)

- [Using Docker for building and testing](https://github.com/lyft/envoy/tree/master/ci)

- [Guide to contributing to Envoy](https://github.com/lyft/envoy/blob/master/CONTRIBUTING.md)

- [Envoy filter example project (how to consume and extend Envoy as a submodule)](https://github.com/lyft/envoy-filter-example)
