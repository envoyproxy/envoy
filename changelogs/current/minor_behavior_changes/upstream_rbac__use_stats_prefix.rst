Upstream HTTP filters now correctly scope their stats under the parent's stat prefix.
When used as a router upstream filter the stats appear under ``http.<stat_prefix>.rbac.*``
(e.g. ``http.ingress_http.rbac.allowed``); when used as a cluster upstream filter they appear
under ``cluster.<name>.rbac.*``. Previously both cases emitted unqualified or double-prefixed
stat names. This behavior is gated by the runtime flag
``envoy.reloadable_features.upstream_http_filters_correct_stats_prefix`` (default ``true``).
