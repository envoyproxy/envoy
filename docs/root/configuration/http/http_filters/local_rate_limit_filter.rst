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

.. note::
  The token bucket is shared across all workers, thus the rate limits are applied per Envoy process.

Example configuration
---------------------

Example filter configuration for a globally set rate limiter (e.g.: all vhosts/routes share the same token bucket):

.. code-block:: yaml

  name: envoy.filters.http.local_ratelimit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
    stat_prefix: http_local_rate_limiter
    token_bucket:
      max_tokens: 10000
      tokens_per_fill: 1000
      fill_interval: 1s
    filter_enabled:
      runtime_key: local_rate_limit_enabled
      default_value:
        numerator: 100
        denominator: HUNDRED
    filter_enforced:
      runtime_key: local_rate_limit_enforced
      default_value:
        numerator: 100
        denominator: HUNDRED
    response_headers_to_add:
      - append: false
        header:
          key: x-local-rate-limit
          value: 'true'


Example filter configuration for a globally disabled rate limiter but enabled for a specific route:

.. code-block:: yaml

  name: envoy.filters.http.local_ratelimit
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
    stat_prefix: http_local_rate_limiter


The route specific configuration:

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/path/with/rate/limit" }
        route: { cluster: service_protected_by_rate_limit }
        typed_per_filter_config:
          envoy.filters.http.local_ratelimit:
            "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
            token_bucket:
              max_tokens: 10000
              tokens_per_fill: 1000
              fill_interval: 1s
            filter_enabled:
              runtime_key: local_rate_limit_enabled
              default_value:
                numerator: 100
                denominator: HUNDRED
            filter_enforced:
              runtime_key: local_rate_limit_enforced
              default_value:
                numerator: 100
                denominator: HUNDRED
            response_headers_to_add:
              - append: false
                header:
                  key: x-local-rate-limit
                  value: 'true'
      - match: { prefix: "/" }
        route: { cluster: default_service }


Note that if this filter is configured as globally disabled and there are no virtual host or route level
token buckets, no rate limiting will be applied.

.. _config_http_filters_local_rate_limit_descriptors:

Using Descriptors to rate limit on
----------------------------------

Descriptors can be used to override local rate limiting based on presence of certain descriptors/route actions.
A route's :ref:`rate limit action <envoy_v3_api_msg_config.route.v3.RateLimit>` is used to match up a
:ref:`local descriptor <envoy_v3_api_msg_extensions.common.ratelimit.v3.LocalRateLimitDescriptor>` in the filter config descriptor list.
The local descriptor's token bucket config is used to decide if the request should be
rate limited or not, if the local descriptor's entries match the route's rate limit actions descriptor entries.
Otherwise the default token bucket config is used.

Example filter configuration using descriptors is as follows:

.. validated-code-block:: yaml
  :type-name:  envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match: { prefix: "/foo" }
        route: { cluster: service_protected_by_rate_limit }
        typed_per_filter_config:
          envoy.filters.http.local_ratelimit:
            "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
            stat_prefix: test
            token_bucket:
              max_tokens: 1000
              tokens_per_fill: 1000
              fill_interval: 60s
            filter_enabled:
              runtime_key: test_enabled
              default_value:
                numerator: 100
                denominator: HUNDRED
            filter_enforced:
              runtime_key: test_enforced
              default_value:
                numerator: 100
                denominator: HUNDRED
            response_headers_to_add:
              - append: false
                header:
                  key: x-test-rate-limit
                  value: 'true'
            descriptors:
            - entries:
              - key: client_id
                value: foo
              - key: path
                value: /foo/bar
              token_bucket:
                max_tokens: 10
                tokens_per_fill: 10
                fill_interval: 60s
            - entries:
              - key: client_id
                value: foo
              - key: path
                value: /foo/bar2
              token_bucket:
                max_tokens: 100
                tokens_per_fill: 100
                fill_interval: 60s
      - match: { prefix: "/" }
        route: { cluster: default_service }
      rate_limits:
      - actions: # any actions in here
        - request_headers:
            header_name: ":path"
            descriptor_key: "path"
        - generic_key:
            descriptor_value: "foo"
            descriptor_key: "client_id"

For this config, requests are ratelimited for routes prefixed with "/foo"
In that, if requests come from client_id "foo" for "/foo/bar" path, then 10 req/min are allowed.
But if they come from client_id "foo" for "/foo/bar2" path, then 100 req/min are allowed.
Otherwise 1000 req/min are allowed.

Statistics
----------

The local rate limit filter outputs statistics in the *<stat_prefix>.http_local_rate_limit.* namespace.
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
  Defaults to 0.

http_filter_enforcing
  % of requests that will enforce the local rate limit decision for a given *route_key* specified in the
  :ref:`local rate limit configuration <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`.
  Defaults to 0. This can be used to test what would happen before fully enforcing the outcome.
