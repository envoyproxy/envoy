.. _config_http_filters_local_rate_limit:

Local rate limit
================

* Local rate limiting :ref:`architecture overview <arch_overview_local_rate_limit>`
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`
* This filter should be configured with the name *envoy.filters.http.local_ratelimit*.

The HTTP local rate limit filter applies a :ref:`token bucket
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.token_bucket>` rate
limit when the request's route or virtual host has a per filter
:ref:`local rate limit configuration <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`.

If the local rate limit token bucket is checked, and there are no token availables, a 429 response is returned
(the response is configurable). The local rate limit filter also sets the
:ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>` header. Additional response
headers may be configured.

Statistics
----------

The local rate limit filter outputs statistics in the *cluster.<route target cluster>.local_ratelimit.* namespace.
429 responses -- or the configured status code -- are emitted to the normal cluster :ref:`dynamic HTTP statistics
<config_cluster_manager_cluster_stats_dynamic_http>`.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  enabled, Counter, Total number of requests for which the rate limiter was consulted
  ok, Counter, Total under limit responses from the token bucket
  rate_limited, Counter, Total responses without an available token (but not necessarily enforced)
  enforced, Counter, Total number of requests for which rate limiting was applied (e.g.: 429 returned)

.. _config_http_filters_local_rate_limit_runtime:

Runtime
-------

The HTTP rate limit filter supports the following runtime fractional settings:

http_filter_enabled
  % of requests that will check the local rate limit decision, but not enforce, for a given *route_key* specified
  in the :ref:`local rate limit configuration <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`.
  Defaults to 100.

http_filter_enforcing
  % of requests that will enforce the local rate limit decision for a given *route_key* specified in the
  :ref:`local rate limit configuration <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`.
  Defaults to 100. This can be used to test what would happen before fully enforcing the outcome.
