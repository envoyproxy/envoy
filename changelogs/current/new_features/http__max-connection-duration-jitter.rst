Added :ref:`max_connection_duration_jitter
<envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration_jitter>` to
``HttpProtocolOptions``. When set, the ``max_connection_duration`` timer is extended by a random
amount up to ``max_connection_duration * jitter / 100``, preventing a thundering-herd of
reconnects when many connections are established at roughly the same time. This follows the same
pattern as TCP proxy's ``max_downstream_connection_duration_jitter_percentage``.
