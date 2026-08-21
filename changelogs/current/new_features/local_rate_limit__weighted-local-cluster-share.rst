Added :ref:`share_mode
<envoy_v3_api_field_extensions.common.ratelimit.v3.LocalClusterRateLimit.share_mode>` to
:ref:`local_cluster_rate_limit
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.local_cluster_rate_limit>`.
The default ``EVEN`` mode keeps the existing behavior of giving each instance ``1 / N`` of the
configured tokens. The new ``WEIGHTED`` mode instead gives each instance
``own_weight / total_weight``, summing the :ref:`load_balancing_weight
<envoy_v3_api_field_config.endpoint.v3.LbEndpoint.load_balancing_weight>` of the local cluster's
endpoints, so that a local cluster with heterogeneous weights gives each instance a share of the
token bucket that tracks its share of the traffic. How an instance is located among those endpoints
is controlled by :ref:`self_identifier
<envoy_v3_api_field_extensions.common.ratelimit.v3.LocalClusterRateLimit.self_identifier>`; an
instance that cannot be located unambiguously falls back to an even share.
