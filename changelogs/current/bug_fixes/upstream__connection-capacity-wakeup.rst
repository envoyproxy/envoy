Fixed pending requests remaining queued after another connection pool releases capacity under a
cluster's connection circuit breaker. Blocked pools are now notified on their worker dispatcher
when capacity becomes available. This fix can be disabled by setting runtime guard
``envoy.reloadable_features.conn_pool_wakeup_on_connection_release`` to ``false``.
