# Developer documentation

Envoy is built using the Bazel build system. Our CI on Azure Pipelines builds, tests, and runs coverage against
all pull requests and the main branch.

To get started building Envoy locally, see the [Bazel quick start](https://github.com/envoyproxy/envoy/blob/main/bazel/README.md#quick-start-bazel-build-for-developers).
To run tests, there are Bazel [targets](https://github.com/envoyproxy/envoy/blob/main/bazel/README.md#testing-envoy-with-bazel) for Google Test.
To generate a coverage report, there is a [coverage build script](https://github.com/envoyproxy/envoy/blob/main/bazel/README.md#coverage-builds).

If you plan to contribute to Envoy, you may find it useful to install the Envoy [development support toolchain](https://github.com/envoyproxy/envoy/blob/main/support/README.md), which helps automate parts of the development process, particularly those involving code review.

Below is a list of additional documentation to aid the development process:

- [General build and installation documentation](https://www.envoyproxy.io/docs/envoy/latest/start/start)

- [Building and testing Envoy with Bazel](https://github.com/envoyproxy/envoy/blob/main/bazel/README.md)

- [Managing external dependencies with Bazel](https://github.com/envoyproxy/envoy/blob/main/bazel/EXTERNAL_DEPS.md)

- [Guide to Envoy Bazel rules (managing `BUILD` files)](https://github.com/envoyproxy/envoy/blob/main/bazel/DEVELOPER.md)

- [Guide to setup development environment with Visual Studio Code](https://github.com/envoyproxy/envoy/blob/main/tools/vscode/README.md)

- [Using Docker for building and testing](https://github.com/envoyproxy/envoy/tree/main/ci#readme)

- [Guide to contributing to Envoy](https://github.com/envoyproxy/envoy/blob/main/CONTRIBUTING.md)

- [Overview of Envoy's testing frameworks](https://github.com/envoyproxy/envoy/blob/main/test/README.md)

- [Overview of how to write integration tests for new code](https://github.com/envoyproxy/envoy/blob/main/test/integration/README.md)

- [Envoy filter example project (how to consume and extend Envoy as a submodule)](https://github.com/envoyproxy/envoy-filter-example#readme)

- [Performance testing Envoy with `tcmalloc`/`pprof`](https://github.com/envoyproxy/envoy/blob/main/bazel/PPROF.md)

And some documents on components of Envoy architecture:

- [Envoy flow control](https://github.com/envoyproxy/envoy/blob/main/source/docs/flow_control.md)

- [Envoy's subset load balancer](https://github.com/envoyproxy/envoy/blob/main/source/docs/subset_load_balancer.md)
