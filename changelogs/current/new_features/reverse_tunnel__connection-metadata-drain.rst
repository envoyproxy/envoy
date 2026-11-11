Added a gradual drain for reverse tunnels on the drain-aware HTTP connection manager. On listener
drain, every connection now drains like an HCM graceful drain instead of receiving an immediate
GOAWAY, whether or not the new fields are set: a soft GOAWAY at drain start, then the final GOAWAY
after ``hcm_config.drain_timeout``, extended by ``hcm_config.drain_timeout_jitter`` if set. When
:ref:`enable_drain_with_goaway
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.enable_drain_with_goaway>`
is set, a draining tunnel instead redials a replacement at drain start and then sends, at offsets
from drain start: a connection-level METADATA frame carrying ``envoy.reverse_tunnel.drain: true`` at
:ref:`metadata_delay
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.metadata_delay>`,
and a soft GOAWAY at :ref:`shutdown_notice_delay
<envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.DrainAwareHttpConnectionManager.shutdown_notice_delay>`,
before the final GOAWAY. Unset delays skip that step, so without ``shutdown_notice_delay`` such a
tunnel receives no GOAWAY until the final one and keeps serving while the replacement is
established. Configs where a step cannot be sent in order are rejected. The upstream reverse-tunnel codec notifies the reverse tunnel reporter when it
receives the METADATA frame. Both sides require ``http2_protocol_options.allow_metadata`` for the
METADATA frame.
