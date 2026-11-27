Fixed the :ref:`rate limit quota <config_http_filters_rate_limit_quota>` filter copying its whole bucket cache
into thread-local storage on every new bucket, RLQS response and assignment expiration.
The copy made bucket creation quadratic on the main thread and stalled it for minutes with ~100k per-user buckets
(admin endpoint unresponsive, xDS streams timing out, usage reports delayed).
Each worker thread now owns its own cache and receives per-entry updates.
