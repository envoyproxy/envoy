Added ``envoy_dynamic_module_callback_upstream_http_tcp_bridge_enable_upstream_connection_reuse``,
which the Rust SDK exposes as ``EnvoyUpstreamHttpTcpBridge::enable_upstream_connection_reuse``, so a
module of the upstream HTTP TCP bridge (``envoy.upstreams.http.dynamic_modules``) can let its
upstream connection serve the next request to the same host once the upstream has sent its whole
reply. Reuse is best effort, so Envoy may still close the connection. Examples are a request that
has not reached the module in full, a connection the module or the upstream half-closed, paused
reads, a write buffer that has not drained since it went over its high watermark, data the module
sent that the socket has not taken yet, and a reset of the request.
