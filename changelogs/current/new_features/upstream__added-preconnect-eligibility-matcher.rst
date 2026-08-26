Added :ref:`preconnect_enabled_metadata
<envoy_v3_api_field_config.cluster.v3.Cluster.PreconnectPolicy.preconnect_enabled_metadata>` to
restrict upstream preconnects to hosts whose endpoint metadata matches the configured matcher.
Non-matching hosts receive connections only for on-demand requests. Suppressed preconnects
increment a new ``upstream_cx_preconnect_skipped`` counter.
