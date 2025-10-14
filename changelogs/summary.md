**Summary of changes**:

* HTTP:
  - **Breaking**: Changed default HTTP/2 max concurrent streams from unlimited to 1024, initial stream window from 256MiB to 16MiB, and connection window from 256MiB to 24MiB for improved memory safety.
  - Added HTTP/1.1 proxy transport RFC 9110 compliant ``CONNECT`` requests with ``Host`` header by default.
  - Enhanced route refresh to trigger tracing refresh, applying new route's sampling and decoration to active spans.
  - Added support for decompressed HTTP header bytes tracking in access logs.
  - Added stream flush timeout configuration independent of stream idle timeout.
  - Added header removal based on header key matching patterns.
  - Added per-route compressor library override support.
  - Added ``upstream_rq_per_cx`` histogram for connection reuse monitoring.

* Security & TLS:
  - Fixed TLS inspector regression that closed plain text connections when reading >16KB at once.
  - Fixed use-after-free in DNS cache when ``Host`` header is modified between filters.
  - Fixed listener socket creation failures in different Linux network namespaces.

* Load Balancing & Networking:
  - Moved locality weighted round robin structures out of ``HostSetImpl`` into separate classes.
  - Added support for weighted cluster hash policies for consistent session affinity.
  - Fixed client-side weighted round robin load balancer priority iteration issues.
  - Added network namespace filepath support to socket addresses for containerized environments.
  - Enhanced network namespace input matching for RBAC and filter chain selection.

* External Processing & Authentication:
  - Re-enabled ``fail_open`` + ``FULL_DUPLEX_STREAMED`` configuration combination.
  - Added per-route gRPC service override and retry policy support for ext_authz.
  - Added configurable HTTP status codes on ext_proc errors and TLS alerts on network ext_authz denials.
  - Added OAuth2 token encryption disable option for trusted environments.
  - Enhanced header count validation after mutations in ext_authz.

* Observability & Stats:
  - Added support for removing unused metrics from memory with configurable eviction intervals.
  - Added stateful session filter statistics for routing outcome monitoring.
  - Added upstream connection recording option to HTTP tap filter.
  - Added GeoIP database build timestamp tracking.
  - Added OAuth2 response code details for ``401`` local responses.

* Dynamic Modules & Extensions:
  - Added logging ABI for modules to emit logs in standard Envoy logging stream.
  - Added support for counters, gauges, histograms in dynamic modules API.
  - Added new Redis commands including ``COPY``, ``RPOPLPUSH``, ``SMOVE``, ``SUNION``, and others.
  - Added reverse tunnel support for NAT/firewall traversal (experimental).

* Runtime & Configuration:
  - Enhanced rate limit filter with substitution formatter support at stream complete phase.
  - Added OTLP stat sink resource attributes and custom metric conversions.
  - Added support for request payloads in HTTP health checks.

* Notable Fixes:
  - Fixed TCP proxy idle timeout handling for new connections.
  - Fixed UDP proxy crash during ``SIGTERM`` with active tunneling sessions.
  - Fixed HTTP/3 access log skipping for half-closed streams.
  - Fixed premature stream resets causing recursive draining and potential stack overflow.
  - Fixed OAuth2 cookie handling in pass-through matcher configurations.
