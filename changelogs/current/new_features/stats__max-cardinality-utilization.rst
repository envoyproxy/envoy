Added ``server.stats_cardinality.max_counter_utilization``, ``server.stats_cardinality.max_gauge_utilization``,
and ``server.stats_cardinality.max_histogram_utilization`` gauges, which report the highest percentage of the
configured per-scope stat cardinality limit in use across all scopes with a limit. These allow alerting before
``server.stats_overflow.*`` counters indicate that stats are being dropped.
