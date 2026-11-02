Added an opt-in gradual drain for reverse tunnels on the drain-aware HTTP connection manager.
When :ref:`enable_drain_with_goaway
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.enable_drain_with_goaway>`
is set, a draining tunnel redials a replacement at drain start and then sends, at offsets from drain
start: a connection-level METADATA frame carrying ``envoy.reverse_tunnel.drain: true`` at
:ref:`metadata_delay
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.metadata_delay>`,
a soft GOAWAY at :ref:`shutdown_notice_delay
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.shutdown_notice_delay>`,
and the final GOAWAY at ``hcm_config.drain_timeout``. Unset delays skip that step, and configs where a step cannot be sent in order are rejected. The upstream
reverse-tunnel codec notifies the reverse tunnel reporter when it receives the METADATA frame. Both
sides require ``http2_protocol_options.allow_metadata`` for the METADATA frame.
