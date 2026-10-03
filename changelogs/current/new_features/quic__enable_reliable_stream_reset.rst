Added :ref:`enable_reliable_stream_reset
<envoy_v3_api_field_config.core.v3.QuicProtocolOptions.enable_reliable_stream_reset>` to
:ref:`QuicProtocolOptions <envoy_v3_api_msg_config.core.v3.QuicProtocolOptions>`. When enabled and
negotiated with the peer, Envoy uses QUIC ``RESET_STREAM_AT`` on stream abort so data already
written on the stream is still delivered up to the reliable size.
