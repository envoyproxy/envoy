Added :ref:`drain_timeout_jitter
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout_jitter>`
to ``HttpConnectionManager``. When set, the drain grace period (between the shutdown notice
``GOAWAY`` and the final ``GOAWAY``) is extended by a random amount up to ``drain_timeout * jitter / 100``,
staggering the final ``GOAWAY`` across simultaneously-draining connections to mitigate
thundering-herd reconnects.
