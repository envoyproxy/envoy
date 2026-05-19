Added :ref:`drain_percentage
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_percentage>`
to ``HttpConnectionManager``. When ``max_connection_duration`` fires, only this fraction
of connections drain in the current cycle; for the remaining connections the duration
timer is reset and they wait for the next round. Spreads reconnects across multiple
cycles when many long-lived connections share the same duration limit.
