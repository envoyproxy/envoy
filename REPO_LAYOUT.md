# Repository layout overview

This is a high level overview of how the repository is laid out to both aid in code investigation,
as well as to clearly specify how extensions are added to the repository. The top level directories
are:

* [.azure-pipelines/](.azure-pipelines/): Configuration for
[Azure Pipelines](https://azure.microsoft.com/en-us/services/devops/pipelines/).
* [api/](api/): Envoy data plane API.
* [bazel/](bazel/): Configuration for Envoy's use of [Bazel](https://bazel.build/).
* [ci/](ci/): Scripts used both during CI as well as to build Docker containers.
* [configs/](configs/): Example Envoy configurations.
* [docs/](docs/): End user facing Envoy proxy and data plane API documentation as well as scripts
  for publishing final docs during releases.
* [examples/](examples/): Larger Envoy examples using Docker and Docker Compose.
* [include/](include/): "Public" interface headers for "core" Envoy. In general,
  these are almost entirely 100% abstract classes. There are a few cases of not-abstract classes in
  the "public" headers, typically for performance reasons. Note that "core" includes some
  "extensions" such as the HTTP connection manager filter and associated functionality which are
  so fundamental to Envoy that they will likely never be optional from a compilation perspective.
* [restarter/](restarter/): Envoy's hot restart wrapper Python script.
* [source/](source/): Source code for core Envoy as well as extensions. The layout of this directory
  is discussed in further detail below.
* [support/](support/): Development support scripts (pre-commit Git hooks, etc.)
* [test/](test/): Test code for core Envoy as well as extensions. The layout of this directory is
  discussed in further detail below.
* [tools/](tools/): Miscellaneous tools that have not found a home somewhere else.

## [source/](source/)

* [common/](source/common/): Core Envoy code (not specific to extensions) that is also not
  specific to a standalone server implementation. I.e., this is the code that could be used if Envoy
  were eventually embedded as a library.
* [docs/](source/docs/): Miscellaneous developer/design documentation that is not relevant for
  the public user documentation.
* [exe/](source/exe/): Code specific to building the final production Envoy server binary. This is
  the only code that is not shared by integration and unit tests.
* [extensions/](source/extensions/): Extensions to the core Envoy code. The layout of this
  directory is discussed in further detail below.
* [server/](source/server/): Code specific to running Envoy as a standalone server. E.g,
  configuration, server startup, workers, etc. Over time, the line between `common/` and `server/`
  has become somewhat blurred. Use best judgment as to where to place something.

## [test/](test/)

Not every directory within test is described below, but a few highlights:

* Unit tests are found in directories matching their [source/](source/) equivalents. E.g.,
  [common/](test/common/), [exe/](test/exe/), and [server/](test/server/).
* Extension unit tests also match their source equivalents in [extensions/](test/extensions/).
* [integration/](test/integration/) holds end-to-end integration tests using roughly the real
  Envoy server code, fake downstream clients, and fake upstream servers. Integration tests also
  test some of the extensions found in the repository. Note that in the future, we would like to
  allow integration tests that are specific to extensions and are not required for covering
  "core" Envoy functionality. Those integration tests will likely end up in the
  [extensions/](test/extensions/) directory but further work and thinking is required before
  we get to that point.
* [mocks/](test/mocks/) contains mock implementations of all of the core Envoy interfaces found in
  [include/](include/).
* Other directories include tooling used for configuration testing, coverage testing, fuzz testing,
  common test code, etc.

## [source/extensions](source/extensions/) layout

We maintain a very specific code and namespace layout for extensions. This aids in discovering
code/extensions, and also will allow us in the future to more easily scale out our extension
maintainers by having OWNERS files specific to certain extensions. (As of this writing, this is not
currently implemented but that is the plan moving forward.)

* All extensions are either registered in [all_extensions.bzl](source/extensions/all_extensions.bzl)
  or [extensions_build_config.bzl](source/extensions/extensions_build_config.bzl). The former is
  for extensions that cannot be removed from the primary Envoy build. The latter is for extensions
  that can be removed on a site specific basis. See [bazel/README.md](bazel/README.md) for how to
  compile out extensions on a site specific basis. Note that by default extensions should be
  removable from the build unless there is a very good reason.
* These are the top level extension directories and associated namespaces:
  * [access_loggers/](/source/extensions/access_loggers): Access log implementations which use
    the `Envoy::Extensions::AccessLoggers` namespace.
  * [filters/http/](/source/extensions/filters/http): HTTP L7 filters which use the
    `Envoy::Extensions::HttpFilters` namespace.
  * [filters/listener/](/source/extensions/filters/listener): Listener filters which use the
    `Envoy::Extensions::ListenerFilters` namespace.
  * [filters/network/](/source/extensions/filters/network): L4 network filters which use the
    `Envoy::Extensions::NetworkFilters` namespace.
  * [grpc_credentials/](/source/extensions/grpc_credentials): Custom gRPC credentials which use the
    `Envoy::Extensions::GrpcCredentials` namespace.
  * [health_checker/](/source/extensions/health_checker): Custom health checkers which use the
    `Envoy::Extensions::HealthCheckers` namespace.
  * [resolvers/](/source/extensions/resolvers): Network address resolvers which use the
    `Envoy::Extensions::Resolvers` namespace.
  * [stat_sinks/](/source/extensions/stat_sinks): Stat sink implementations which use the
    `Envoy::Extensions::StatSinks` namespace.
  * [tracers/](/source/extensions/tracers): Tracers which use the
    `Envoy::Extensions::Tracers` namespace.
  * [transport_sockets/](/source/extensions/transport_sockets): Transport socket implementations
    which use the `Envoy::Extensions::TransportSockets` namespace.
* Each extension is contained wholly in its own namespace. E.g.,
  `Envoy::Extensions::NetworkFilters::Echo`.
* Common code that is used by multiple extensions should be in a `common/` directory as close to
  the extensions as possible. E.g., [filters/common/](/source/extensions/filters/common) for common
  code that is used by both HTTP and network filters. Common code used only by two HTTP filters
  would be found in `filters/http/common/`. Common code should be placed in a common namespace.
  E.g., `Envoy::Extensions::Filters::Common`.
