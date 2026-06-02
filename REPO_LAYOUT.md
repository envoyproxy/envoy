# Repository layout overview

This is a high level overview of how the repository is laid out to both aid in code investigation,
as well as to clearly specify how extensions are added to the repository. The top level directories
are:

* [.github/](.github/): GitHub configuration including CI workflows, issue templates, and bot
  configuration.
* [api/](api/): Envoy data plane API.
* [bazel/](bazel/): Configuration for Envoy's use of [Bazel](https://bazel.build/).
* [changelogs/](changelogs/): Release changelogs for each Envoy version.
* [ci/](ci/): Scripts used both during CI as well as to build Docker containers.
* [compat/](compat/): OpenSSL compatibility layer.
* [configs/](configs/): Example Envoy configurations.
* [contrib/](contrib/): Contrib extensions (non-core). See [EXTENSION_POLICY.md](EXTENSION_POLICY.md)
  for more information. Layout is discussed in further detail below.
* [distribution/](distribution/): Packaging and distribution scripts (Debian packages, Docker
  images, etc.).
* [docs/](docs/): End user facing Envoy proxy and data plane API documentation as well as scripts
  for publishing final docs during releases.
* [envoy/](envoy/): "Public" interface headers for "core" Envoy. In general,
  these are almost entirely 100% abstract classes. There are a few cases of not-abstract classes in
  the "public" headers, typically for performance reasons. Note that "core" includes some
  "extensions" such as the HTTP connection manager filter and associated functionality which are
  so fundamental to Envoy that they will likely never be optional from a compilation perspective.
* [linux/](linux/): Linux platform-specific configuration (e.g., amd64).
* [maintainer/](maintainer/): Release process documentation for maintainers.
* [mobile/](mobile/): Envoy Mobile — library for using Envoy on iOS and Android platforms.
* [restarter/](restarter/): Envoy's hot restart wrapper Python script.
* [security/](security/): Some templates for reporting security issues of Envoy. Historical security issues can also be found here.
* [source/](source/): Source code for core Envoy as well as extensions. The layout of this directory
  is discussed in further detail below.
* [support/](support/): Development support scripts (pre-commit Git hooks, etc.)
* [test/](test/): Test code for core Envoy as well as extensions. The layout of this directory is
  discussed in further detail below.
* [third_party/](third_party/): Third-party dependencies (e.g., Android platform support).
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
  [envoy/](envoy/).
* Other directories include tooling used for configuration testing, coverage testing, fuzz testing,
  common test code, etc.

## [source/extensions](source/extensions/) layout

We maintain a very specific code and namespace layout for extensions. This aids in discovering
code/extensions, and allows us specify extension owners in [CODEOWNERS](CODEOWNERS).

* All extensions are either registered in [all_extensions.bzl](source/extensions/all_extensions.bzl)
  or [extensions_build_config.bzl](source/extensions/extensions_build_config.bzl). The former is
  for extensions that cannot be removed from the primary Envoy build. The latter is for extensions
  that can be removed on a site specific basis. See [bazel/README.md](bazel/README.md) for how to
  compile out extensions on a site specific basis. Note that by default extensions should be
  removable from the build unless there is a very good reason.
* These are the top level extension directories and associated namespaces:
  * [access_loggers/](/source/extensions/access_loggers): Access log implementations which use
    the `Envoy::Extensions::AccessLoggers` namespace.
  * [api_listeners/](/source/extensions/api_listeners): API listener implementations which use
    the `Envoy::Extensions::ApiListeners` namespace.
  * [bootstrap/](/source/extensions/bootstrap): Bootstrap extensions which use
    the `Envoy::Extensions::Bootstrap` namespace.
  * [clusters/](/source/extensions/clusters): Cluster extensions which use the
    `Envoy::Extensions::Clusters` namespace.
  * [compression/](/source/extensions/compression): Compression extensions
    which use `Envoy::Extensions::Compression` namespace.
  * [config/](/source/extensions/config): Config extensions which use
    the `Envoy::Extensions::Config` namespace.
  * [config_subscription/](/source/extensions/config_subscription): Config subscription
    implementations which use the `Envoy::Config` namespace.
  * [content_parsers/](/source/extensions/content_parsers): Content parser extensions which use
    the `Envoy::Extensions::ContentParsers` namespace.
  * [dynamic_modules/](/source/extensions/dynamic_modules): Dynamic module extensions which use
    the `Envoy::Extensions::DynamicModules` namespace.
  * [early_data/](/source/extensions/early_data): Early data (0-RTT) extensions which use
    the `Envoy::Router` namespace.
  * [fatal_actions/](/source/extensions/fatal_actions): Fatal Action extensions
    which use the `Envoy::Extensions::FatalActions` namespace.
  * [filters/http/](/source/extensions/filters/http): HTTP L7 filters which use the
    `Envoy::Extensions::HttpFilters` namespace.
  * [filters/listener/](/source/extensions/filters/listener): Listener filters which use the
    `Envoy::Extensions::ListenerFilters` namespace.
  * [filters/network/](/source/extensions/filters/network): L4 network filters which use the
    `Envoy::Extensions::NetworkFilters` namespace.
  * [formatter/](/source/extensions/formatter): Access log formatters which use the
    `Envoy::Extensions::Formatter` namespace.
  * [geoip_providers/](/source/extensions/geoip_providers): Geoip provider implementations
    which use the `Envoy::Extensions::GeoipProviders` namespace.
  * [grpc_credentials/](/source/extensions/grpc_credentials): Custom gRPC credentials which use the
    `Envoy::Extensions::GrpcCredentials` namespace.
  * [health_check/](/source/extensions/health_check): Health check implementations which use
    the `Envoy::Upstream` namespace.
  * [health_checkers/](/source/extensions/health_checkers): Custom health checkers which use the
    `Envoy::Upstream` namespace.
  * [http/](/source/extensions/http): HTTP utility extensions which use
    the `Envoy::Extensions::HttpFilters` namespace.
  * [internal_redirect/](/source/extensions/internal_redirect): Internal Redirect
    extensions which use the `Envoy::Extensions::InternalRedirect` namespace.
  * [io_socket/](/source/extensions/io_socket): IO socket implementations which use
    the `Envoy::Extensions::IoSocket` namespace.
  * [key_value/](/source/extensions/key_value): Key-value store extensions which use
    the `Envoy::Extensions::KeyValue` namespace.
  * [listener_managers/](/source/extensions/listener_managers): Listener manager implementations
    which use the `Envoy::Server` namespace.
  * [load_balancing_policies/](/source/extensions/load_balancing_policies): Load balancing policy
    extensions which use the `Envoy::Extensions::LoadBalancingPolicies` namespace.
  * [local_address_selectors/](/source/extensions/local_address_selectors): Local address selector
    extensions which use the `Envoy::Extensions::LocalAddressSelectors` namespace.
  * [matching/](/source/extensions/matching): Matching extensions which use
    the `Envoy::Extensions::Matching` namespace.
  * [network/](/source/extensions/network): Network utility extensions which use
    the `Envoy::Network` namespace.
  * [path/](/source/extensions/path): Path transformation extensions which use
    the `Envoy::Extensions::UriTemplate` namespace.
  * [quic/](/source/extensions/quic): QUIC extensions which use the `Envoy::Quic` namespace.
  * [rate_limit_descriptors/](/source/extensions/rate_limit_descriptors): Rate limit
    descriptor extensions use the `Envoy::Extensions::RateLimitDescriptors`
    namespace.
  * [request_id/](/source/extensions/request_id): Request ID extensions which use
    the `Envoy::Extensions::RequestId` namespace.
  * [resource_monitors/](/source/extensions/resource_monitors): Resource monitor
    extensions which use the `Envoy::Extensions::ResourceMonitors` namespace.
  * [retry/](/source/extensions/retry): Retry extensions which use the
    `Envoy::Extensions::Retry` namespace.
  * [router/](/source/extensions/router): Router extensions which use
    the `Envoy::Extensions::Router` namespace.
  * [stat_sinks/](/source/extensions/stat_sinks): Stat sink implementations which use the
    `Envoy::Extensions::StatSinks` namespace.
  * [string_matcher/](/source/extensions/string_matcher): String matcher extensions which use
    the `Envoy::Extensions::StringMatcher` namespace.
  * [tracers/](/source/extensions/tracers): Tracers which use the
    `Envoy::Extensions::Tracers` namespace.
  * [transport_sockets/](/source/extensions/transport_sockets): Transport socket implementations
    which use the `Envoy::Extensions::TransportSockets` namespace.
  * [udp_packet_writer/](/source/extensions/udp_packet_writer): UDP packet writer implementations
    which use the `Envoy::Network` namespace.
  * [upstreams/](/source/extensions/upstreams): Upstream extensions use the
    `Envoy::Extensions::Upstreams` namespace.
  * [wasm_runtime/](/source/extensions/wasm_runtime): Wasm runtime extensions which use
    the `Envoy::Extensions::Common::Wasm` namespace.
  * [watchdog/](/source/extensions/watchdog): Watchdog extensions use the
    `Envoy::Extensions::Watchdog` namespace.
* Each extension is contained wholly in its own namespace. E.g.,
  `Envoy::Extensions::NetworkFilters::Echo`.
* Common code that is used by multiple extensions should be in a `common/` directory as close to
  the extensions as possible. E.g., [filters/common/](/source/extensions/filters/common) for common
  code that is used by both HTTP and network filters. Common code used only by two HTTP filters
  would be found in `filters/http/common/`. Common code should be placed in a common namespace.
  E.g., `Envoy::Extensions::Filters::Common`.

## [contrib](contrib/) layout

This directory contains contrib extensions. See [EXTENSION_POLICY.md](EXTENSION_POLICY.md) for
more information.

* [contrib/exe/](contrib/exe/): The default executable for contrib. This is similar to the
  `envoy-static` target but also includes all contrib extensions, and is used to produce the
  contrib image targets.
* [contrib/...](contrib/): The rest of this directory mirrors the [source/extensions](source/extensions/)
  layout. Contrib extensions are placed here.
