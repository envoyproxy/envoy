Added support to remove unused metrics from memory for extensions that
support evictable metrics. This is done :ref:`periodically
<envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_eviction_interval>`
during the metric flush.
