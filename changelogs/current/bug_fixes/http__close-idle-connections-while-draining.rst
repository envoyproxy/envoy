Fixed idle downstream HTTP connections to begin draining when server draining starts, rather than
waiting for the normal connection idle timeout. Connections with an initialized HTTP codec use one
configured :ref:`drain timeout
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`
grace period; connections that remain without a codec close without waiting for that period.
