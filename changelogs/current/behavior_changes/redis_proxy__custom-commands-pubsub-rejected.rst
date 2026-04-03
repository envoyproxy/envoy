The Redis proxy now rejects, at configuration load, any
:ref:`custom_commands <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.custom_commands>`
entry that names a pub/sub-owned command — ``subscribe``, ``unsubscribe``, ``psubscribe``,
``punsubscribe``, ``ssubscribe``, ``sunsubscribe``, ``publish``, or ``spublish`` (case-insensitive).
These verbs are now handled by the built-in RESP3 pub/sub support, so a configuration that previously
listed one of them in ``custom_commands`` as a generic pass-through (for example
``custom_commands: [publish]``) will now fail to load with a clear ``EnvoyException`` instead of
silently shadowing — and defeating — the built-in handler. To migrate, remove the pub/sub verbs from
``custom_commands``; their built-in pub/sub behavior replaces the pass-through.
