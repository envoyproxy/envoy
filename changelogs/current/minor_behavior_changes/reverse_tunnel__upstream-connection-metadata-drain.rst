Updated the reverse-tunnel upstream HTTP/2 codec so when :ref:`enable_drain_with_goaway
<envoy_v3_api_field_extensions.upstreams.http.reverse_tunnel.v3.ReverseTunnelUpstreamCodecOptions.enable_drain_with_goaway>`
is enabled, upstream-initiated graceful drains (such as via ``/reverse_tunnel/drain_clusters``)
emit a connection-level (stream 0) METADATA frame carrying ``envoy.reverse_tunnel.drain: true``
before the final GOAWAY, replacing the previous drain-start client GOAWAY (which never reached the
wire because nghttp2 rejects client GOAWAYs with an odd last stream ID and oghttp2 drops client
GOAWAYs, yet ``reverse_tunnel_upstream_codec.goaway_sent`` counted it). Replaced
``reverse_tunnel_upstream_codec.goaway_sent`` with ``reverse_tunnel_upstream_codec.drain_started``
to count each drain start, while the cluster's ``http2.goaway_sent`` stat tracks the final GOAWAY
sent after the drain window. The server-side drain-aware HTTP connection manager now observes that
METADATA frame when :ref:`enable_drain_with_goaway
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.enable_drain_with_goaway>`
is enabled and dials a replacement tunnel before the final GOAWAY arrives. Emitting and observing
the drain METADATA frame requires ``http2_protocol_options.allow_metadata`` on both sides.
