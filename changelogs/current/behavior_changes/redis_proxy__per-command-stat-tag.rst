Downstream Redis command counters and latency histograms now expose the ``envoy.redis_command`` tag.
For example, update Prometheus queries for ``envoy_redis_command_get_total`` to
``envoy_redis_command_total{envoy_redis_command="get"}``. The flat stats names are preserved.
