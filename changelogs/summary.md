**Summary of changes**:

* Security:
  - Fixed TLS inspector handling of client hello messages larger than 16KB.
  - Fixed bug where empty trusted CA files were accepted, causing validation of any certificate chain.

* Build:
  - **Major**: Upgraded to C++20, enabling modern C++ features throughout the codebase.
  - Consolidated clang/gcc toolchains using ``--config=clang`` or ``--config=gcc``.
  - **Breaking**: Removed ``grpc_credentials/aws_iam`` extension and contrib squash filter.

* HTTP:
  - Added ``x-envoy-original-host`` header to record original host values before mutation.
  - Added HTTP/3 pseudo header validation (disable via ``envoy.restart_features.validate_http3_pseudo_headers``).
  - Fixed HTTP/1 parser to properly handle newlines between requests per RFC 9112.
  - Added request/response trailer mutations support in header mutation filter.

* Load balancing:
  - Added override host load balancing policy.
  - Added hash policy configuration directly to ring hash and maglev load balancers.
  - Added matcher-based cluster specifier plugin for dynamic cluster selection.

* External processing:
  - Added ``FULL_DUPLEX_STREAMED`` body mode for bidirectional streaming.
  - Implemented graceful gRPC side stream closing with timeout.
  - Added per-route ``failure_mode_allow`` override support.

* Authentication:
  - Added OAuth2 token encryption, configurable token expiration, and OIDC logout support.
  - Added API key auth filter with forwarding configuration.
  - Added AWS IAM Roles Anywhere support.

* Observability:
  - Added TLS certificate expiration metrics.
  - Enhanced transport tap with streaming trace capability.
  - Added JA4 fingerprinting to TLS inspector.
  - Added TCP tunneling access log substitution strings.

* New features:
  - Dynamic modules: Added support for ``LocalityLbEndpoints`` metadata and SSL connection info attributes.
  - Stateful session cookie attributes and envelope mode support.
  - Redis proxy AWS IAM authentication and ``scan``/``info`` command support.
  - Lua filter access to filter context and typed metadata.
  - ``ServerNameMatcher`` for trie-based domain matching.

* Notable fixes:
  - Fixed Wasm hang after VM crash in request callbacks.
  - Fixed Lua filter crash when removing status header.
  - Fixed connection pool capacity calculation issues.
  - Improved TCP proxy retry logic to avoid connection issues.
