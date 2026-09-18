Added :ref:`share_mode
<envoy_v3_api_field_extensions.common.ratelimit.v3.LocalClusterRateLimit.share_mode>` to
:ref:`local_cluster_rate_limit
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.local_cluster_rate_limit>`.
The default ``EVEN`` mode keeps the existing behavior of giving each instance ``1 / N`` of the
configured tokens. The new ``WEIGHTED`` mode instead gives each instance
``own_weight / total_weight``, where ``total_weight`` sums the :ref:`load_balancing_weight
<envoy_v3_api_field_config.endpoint.v3.LbEndpoint.load_balancing_weight>` of the local cluster's
endpoints and ``own_weight`` comes from ``envoy.local_ratelimit``/``self_weight`` in :ref:`node
metadata <envoy_v3_api_field_config.core.v3.Node.metadata>`, so that a local cluster with
heterogeneous weights gives each instance a share of the token bucket that tracks its share of the
traffic. An instance whose own weight is not in its node metadata falls back to the smallest
endpoint weight in the local cluster, and the new
``local_rate_limit.local_cluster_share.self_weight_not_found`` gauge reports it.
