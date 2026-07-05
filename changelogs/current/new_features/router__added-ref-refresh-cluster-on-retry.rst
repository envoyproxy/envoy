Added :ref:`refresh_cluster_on_retry <envoy_v3_api_field_config.route.v3.RetryPolicy.refresh_cluster_on_retry>`
to :ref:`retry policies <envoy_v3_api_msg_config.route.v3.RetryPolicy>` so retry attempts can
refresh the route-selected upstream cluster before being sent. This enables cross-cluster
retries for dynamic cluster selection such as the :ref:`matcher cluster specifier
<envoy_v3_api_msg_extensions.router.cluster_specifiers.matcher.v3.MatcherClusterSpecifier>`.
