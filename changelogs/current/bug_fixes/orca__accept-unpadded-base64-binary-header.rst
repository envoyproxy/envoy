Fixed parsing of ORCA load reports sent as base64-encoded binary headers without padding, such as
the ``endpoint-load-metrics-bin`` header emitted by grpc-go. These reports were previously rejected
with ``unable to decode ORCA binary header value``.
