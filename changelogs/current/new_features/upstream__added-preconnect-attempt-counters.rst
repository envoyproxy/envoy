Added the ``upstream_cx_preconnect_started`` and ``upstream_cx_preconnect_blocked`` counters, which
attribute anticipatory (preconnect) connection attempts as either opened or refused (circuit breaker
open, load shed, or connection creation failure), including connections opened to maintain the eager
preconnect floor. The eager preconnect floor connection-pool behavior is guarded by
``envoy.reloadable_features.eager_preconnect_floor`` and can be disabled by setting that runtime
guard to ``false``.
