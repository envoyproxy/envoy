Fixed the HTTP/3 upstream connection pool during 0-RTT when the stream limit resumed from cached
transport parameters is zero. QUICHE only reports increases, so the pool kept its optimistic capacity
until the handshake completed and could attach a stream QUICHE had no stream ID for.
This change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.quic_report_zero_stream_limit_before_handshake`` to ``false``.
