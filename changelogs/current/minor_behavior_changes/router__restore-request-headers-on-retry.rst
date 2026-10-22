When the upstream HTTP filters run for an attempt, every retry of the request now starts from the request headers
as they were before those filters ran, instead of from the headers the previous attempt's filters left. One
attempt's path rewrites or injected credentials no longer reach the next attempt, including a retry to another
cluster with :ref:`refresh_cluster_on_retry
<envoy_v3_api_field_config.route.v3.RetryPolicy.refresh_cluster_on_retry>`. The router re-creates the request
headers for such a retry, so filters must not keep references into their values across callbacks. Hedged requests
are unchanged. This behavior can be reverted by setting runtime guard
``envoy.reloadable_features.restore_request_headers_on_retry`` to ``false``.
