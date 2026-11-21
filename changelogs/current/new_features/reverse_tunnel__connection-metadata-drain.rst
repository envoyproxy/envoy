Added a gradual drain for reverse tunnels on the drain-aware HTTP connection manager. On listener
drain, established HTTP/2 connections now drain like an HCM graceful drain instead of receiving an
immediate GOAWAY, whether or not the new fields are set: a soft GOAWAY at drain start, then the final GOAWAY
after ``hcm_config.drain_timeout``, extended by ``hcm_config.drain_timeout_jitter`` if set. When
:ref:`enable_drain_with_goaway
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.enable_drain_with_goaway>`
is set, per-connection rotation and in-place filter-chain updates redial a replacement at drain start;
whole-listener draining instead stops pending handshakes and replacement dials. Such a tunnel sends,
at offsets from drain start: a connection-level METADATA frame carrying ``envoy.reverse_tunnel.drain: true`` at
:ref:`metadata_delay
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.metadata_delay>`,
and a soft GOAWAY at :ref:`shutdown_notice_delay
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.shutdown_notice_delay>`,
before the final GOAWAY. Unset delays skip that step, so without ``shutdown_notice_delay`` such a
tunnel receives no GOAWAY until the final one and keeps serving during the grace period. Configs where
a step cannot be sent in order are rejected. The upstream reverse-tunnel codec notifies the reverse
tunnel reporter when it receives the METADATA frame. Both sides require ``http2_protocol_options.allow_metadata`` for the
METADATA frame.
