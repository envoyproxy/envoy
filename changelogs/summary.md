**Summary of changes**:

* Security:
  - [CVE-2025-30157](https://github.com/envoyproxy/envoy/security/advisories/GHSA-cf3q-gqg7-3fm9): Fixed a bug where local replies were incorrectly sent to the ext_proc server.
  - [CVE-2025-31498](https://github.com/c-ares/c-ares/security/advisories/GHSA-6hxc-62jh-p29v): Updated c-ares to version 1.34.5 to address a security vulnerability.

* HTTP:
  - Added support for async load balancing, allowing endpoints to respond with their ability to handle requests.
  - Improved HTTP/1 parser to handle newlines between requests correctly per RFC 9112.
  - Added option to ignore specific HTTP/1.1 upgrade values using configurable matchers.
  - Implemented TCP proxy option to read from downstream connections before establishing upstream connections.

* Performance:
  - Improved performance for HTTP/1 ignored upgrades.
  - Enhanced TCP proxy retries to run in a different event loop iteration to avoid connection issues.
  - Added fixed value option for minimum RTT in adaptive concurrency filter.
  - Enhanced dynamic forward proxy with async lookups for null hosts.

* Reliability:
  - Fixed a bug in preconnecting logic that could lead to excessive connection establishment.
  - Fixed port exhaustion issues in the original_src filter by setting the `IP_BIND_ADDRESS_NO_PORT` socket option.
  - Fixed socket option application for additional listener addresses.
  - Fixed crash when creating an EDS cluster with invalid configuration.

* Features:
  - Added support for loading shared libraries at runtime through dynamic modules.
  - Added support for io_uring in the default socket interface.
  - Extended the compression filter with the ability to skip compression for specific response codes.
  - Added support for QUIC-LB draft standard for connection ID generation.
  - Enhanced ext_proc with graceful gRPC side stream closing and added a new `FULL_DUPLEX_STREAMED` body mode.
  - Introduced PKCE support for OAuth2 authorization code flow and SameSite cookie attribute configuration.
  - Added support for monitoring container CPU utilization in Linux Kubernetes environments.
  - Enhanced proxy protocol TLV support to enable more flexible and customizable usage between downstream and upstream connections.
  - Added multiple formatter attributes improvements, e.g., `QUERY_PARAM`, `CUSTOM_FLAGS`, and `PATH`

* Observability:
  - Enhanced Transport Tap with connection information output per event.
  - Added support for directing LRS to report loads when requests are issued.
