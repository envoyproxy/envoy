Added an opt-in drain-aware upstream HTTP/2 client codec for ``envoy.clusters.reverse_connection``
clusters, configured via ``typed_extension_protocol_options``. When enabled, a draining upstream
gracefully drains its reverse tunnels so peers can fail over to a sibling upstream before in-flight
streams are reset.
