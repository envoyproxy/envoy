Added an opt-in, three-phase gradual drain for reverse tunnels driven by connection-level
(HTTP/2 stream 0) METADATA frames. When the new
:ref:`drain_connection_metadata
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.drain_connection_metadata>`
block is set on the drain-aware HTTP connection manager, a draining tunnel now sends, in order:
(1) a METADATA frame carrying the configured ``metadata_key`` at ``delay``, (2) a soft GOAWAY
(``shutdownNotice``) at ``shutdown_notice_delay`` so the peer stops opening new streams on the old
tunnel, and (3) the final GOAWAY at ``hcm_config.drain_timeout``. The replacement tunnel is
redialed at drain start, so in-flight streams finish on the old tunnel while new traffic moves to
the replacement. The upstream (client) side observes the METADATA frame via the new
:ref:`metadata_key <envoy_v3_api_field_extensions.upstreams.http.reverse_tunnel.v3.ReverseTunnelUpstreamCodecOptions.metadata_key>`
option on the reverse-tunnel upstream codec and notifies the reverse tunnel reporter, mirroring the
server-side drain-aware HCM. ``delay`` must be strictly less than ``shutdown_notice_delay``, which
must be strictly less than ``hcm_config.drain_timeout``. Both sides require
``http2_protocol_options.allow_metadata`` so the METADATA frame is sent and received.
