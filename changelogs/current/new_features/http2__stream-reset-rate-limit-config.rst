Added :ref:`stream_reset_burst <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.stream_reset_burst>`
and :ref:`stream_reset_rate <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.stream_reset_rate>`
fields to ``Http2ProtocolOptions``. These configure the token-bucket RST_STREAM rate limiter
built into ``nghttp2`` (CVE-2023-44487 / HTTP/2 Rapid Reset protection) for server-side connections.
The defaults (burst=1000, rate=33/sec) match the existing ``nghttp2`` behavior; operators can raise
these limits when legitimate workloads (e.g. gRPC streaming with many short-lived streams that
receive RST_STREAM on RESOURCE_EXHAUSTED) exhaust the budget faster than it replenishes.
These options only apply when using ``nghttp2`` as a server.
