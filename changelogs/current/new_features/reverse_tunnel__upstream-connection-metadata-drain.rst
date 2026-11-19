Added :ref:`send_drain_metadata
<envoy_v3_api_field_extensions.upstreams.http.reverse_tunnel.v3.ReverseTunnelUpstreamCodecOptions.send_drain_metadata>`
to the reverse-tunnel upstream HTTP/2 codec so upstream-initiated graceful drains (such as via
``/reverse_tunnel/drain_clusters``) emit a connection-level (stream 0) METADATA frame carrying
``envoy.reverse_tunnel.drain: true`` before the final GOAWAY, replacing the client-side graceful
GOAWAY. The server-side drain-aware HTTP connection manager now observes that METADATA frame when
:ref:`enable_drain_with_goaway
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.enable_drain_with_goaway>`
is enabled and dials a replacement tunnel before the final GOAWAY arrives. Requires
``enable_drain_with_goaway`` on the upstream codec (otherwise config is rejected) and
``http2_protocol_options.allow_metadata`` on both sides.
