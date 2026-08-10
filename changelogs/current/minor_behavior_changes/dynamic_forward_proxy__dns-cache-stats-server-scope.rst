DNS cache statistics (``dns_cache.*``) are now always created under the server-wide stats scope
instead of a caller-derived scope captured when the manager singleton was first instantiated. As
a result they are now matched against the global
:ref:`stats matcher <envoy_v3_api_field_config.metrics.v3.StatsConfig.stats_matcher>`
rather than a per-listener stats matcher. Statistic names are unchanged, so this only affects
configurations where the listener that first created a DNS cache declared a per-listener stats
matcher.
