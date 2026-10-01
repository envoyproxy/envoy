Fixed idle downstream HTTP connections to use the configured :ref:`drain timeout
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`
during server draining instead of waiting for the normal connection idle timeout.
