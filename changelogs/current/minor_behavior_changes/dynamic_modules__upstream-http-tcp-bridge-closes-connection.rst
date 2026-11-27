The upstream HTTP TCP bridge (``envoy.upstreams.http.dynamic_modules``) now closes its upstream
connection when a request ends, so each request opens a new connection, with its own handshake on a
TLS cluster, unless the module enables reuse of it with
``envoy_dynamic_module_callback_upstream_http_tcp_bridge_enable_upstream_connection_reuse``.
Only the module knows where its protocol's exchange ends, and the bridge used to return the
connection to the TCP connection pool after a completed request. The next request to that host on
the same worker could then get a connection the module had half-closed, which failed an assertion
in debug builds and dropped the request's upstream data in release builds, or receive bytes the
upstream sent after the earlier response ended. Envoy closes these connections first, so each such
request leaves a socket in TIME_WAIT on the Envoy host, and a module that sends many requests to one
host without enabling reuse can run out of local ports.
