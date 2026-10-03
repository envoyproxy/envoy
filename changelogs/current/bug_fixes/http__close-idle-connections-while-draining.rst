Fixed idle downstream HTTP connections to begin draining when server draining starts, rather than
waiting for the normal connection idle timeout. Connections with an initialized HTTP codec enter the
HTTP drain sequence immediately, using the configured :ref:`drain timeout
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`.
Connections without a codec close after that timeout with reason
``drained_connection_without_codec``. These drain paths increment ``downstream_cx_drain_close``.
If a codec is initialized during the wait, a new drain timeout begins for the HTTP drain sequence.
