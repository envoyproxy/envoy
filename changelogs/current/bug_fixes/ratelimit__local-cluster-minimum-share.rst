Added the disabled-by-default runtime guard
``envoy.reloadable_features.local_ratelimit_local_cluster_minimum_one_token``. When enabled, each
non-zero local cluster token bucket keeps one token of effective capacity per Envoy when membership exceeds
``max_tokens``. This favors non-zero local capacity but can make the aggregate burst exceed
``max_tokens`` and the aggregate admitted rate exceed the configured refill rate. Users that require
an intentional total block should use ``max_tokens: 0``.
