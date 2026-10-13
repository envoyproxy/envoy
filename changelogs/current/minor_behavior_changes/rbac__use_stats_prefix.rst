Upstream HTTP filter stats are now correctly scoped under the parent's stat prefix
(``http.<stat_prefix>.rbac.*`` for router filters, ``cluster.<name>.rbac.*`` for cluster
filters). Guarded by runtime flag
``envoy.reloadable_features.upstream_http_filters_correct_stats_prefix`` (default ``true``).
