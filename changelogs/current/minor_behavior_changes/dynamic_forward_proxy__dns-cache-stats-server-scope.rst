DNS cache statistics (``dns_cache.*``) are now created under the server-wide stats scope
instead of the stats scope of whichever listener or cluster first loaded the cache. As a
result they are now matched against the global
:ref:`stats matcher <envoy_v3_api_field_config.metrics.v3.StatsConfig.stats_matcher>`
rather than a per-listener stats matcher.
