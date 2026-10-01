Fixed idle downstream HTTP connections to begin the graceful drain sequence when server draining
starts, using one configured :ref:`drain timeout
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`
grace period instead of waiting for the normal connection idle timeout.
