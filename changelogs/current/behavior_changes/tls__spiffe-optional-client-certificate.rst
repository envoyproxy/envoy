Fixed the SPIFFE certificate validator to honor
:ref:`require_client_certificate
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.require_client_certificate>`.
When this field is ``false`` or unset, downstream connections without a client certificate are now
accepted while presented client certificates are still validated.
