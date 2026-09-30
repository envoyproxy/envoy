Fixed parsing of ORCA load reports sent as base64-encoded binary headers without padding, such as
the ``endpoint-load-metrics-bin`` header emitted by grpc-go. These reports were previously rejected
with ``unable to decode ORCA binary header value``. This change can be reverted by setting runtime
guard ``envoy.reloadable_features.orca_accept_unpadded_base64`` to ``false``.
