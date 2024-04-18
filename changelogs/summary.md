**Summary of changes**:

* Removed the Swift/C++ interop layer in Envoy Mobile.
* Addd retry policy to ext_proc.
* Added HTTP downstream remote reset response flag.
* Added support for the Fluentd access logger.
* Introduced `MemoryAllocatorManager` to configure heap memory release rate.
* Envoy Mobile added `CONNECT` Proxy support for iOS.
* Redis: support echo command.
* Envoy Mobile setting QUIC newtowrk idle timeout to 30 seconds.
* Sending server preferred address to non-QUICHE clients.
* Avoid concatenation of JWT duplicated headers.
* HTTP: Keep `Transfer-Encoding` header for `trailers`.
* Envoy Mobile setting the socket receive buffer to 1MB for QUIC.
* Added `FULL_SCAN` support to least-request load-balancing algorithm.
* aws_lambda and ext_proc filters can be used as an upstream filter.
* Hosts marked as draining in and EDS update are now excluded.
* Envoy Mobile supports log-levels.
* Added support for URI tempate matching for RBAC.
* Fixed load balancing initialization bug.
* Supporting `%UPSTREAM_CONNECTION_ID%` in access logs.
* Added request and response attributes support to ext_proc.
* Added support sending dynamic metadata to ext_proc.
* Re-enable the nghttp2 codec for HTTP/2 connections by default.
