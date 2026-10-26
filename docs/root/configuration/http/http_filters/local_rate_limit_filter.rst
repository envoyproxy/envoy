.. _config_http_filters_local_rate_limit:

Local rate limit
================

* Local rate limiting :ref:`architecture overview <arch_overview_local_rate_limit>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`

The HTTP local rate limit filter applies a :ref:`token bucket
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.token_bucket>` rate
limit when the request's route or virtual host has a per filter
:ref:`local rate limit configuration <envoy_v3_api_msg_extensions.filters.http.local_ratelimit.v3.LocalRateLimit>`.

If the local rate limit token bucket is checked, and there are no tokens available, a 429 response is returned
(the response is configurable). The local rate limit filter then sets the
:ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>` response header. :ref:`Additional response headers
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.response_headers_to_add>` can be
configured to be returned.

:ref:`Request headers
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.request_headers_to_add_when_not_enforced>` can be
configured to be added to forwarded requests to the upstream when the local rate limit filter is enabled but not enforced.

Depending on the value of the config :ref:`local_rate_limit_per_downstream_connection <envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.local_rate_limit_per_downstream_connection>`,
the token bucket is either shared across all workers or on a per connection basis. This results in the local rate limits being applied either per Envoy process or per downstream connection.
By default the rate limits are applied per Envoy process.

Retry-After header
------------------

When :ref:`enable_retry_after_header
<envoy_v3_api_field_extensions.filters.http.local_ratelimit.v3.LocalRateLimit.enable_retry_after_header>`
is enabled and the filter enforces a 429 response, the response includes ``Retry-After`` using the
``delay-seconds`` form. The delay is the number of seconds until the next token is available in the
bucket that rejected the request, clamped to at least one second.
If ``response_headers_to_add`` sets a ``Retry-After`` header, the filter does not overwrite it.

The header is not emitted for an upstream-generated 429 response, when rate limiting is not
enforced, or when a custom status other than 429 is configured. The option is disabled by default.

.. code-block:: yaml

  stat_prefix: local_rate_limiter
  token_bucket:
    max_tokens: 100
    tokens_per_fill: 100
    fill_interval: 60s
  filter_enabled:
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enforced:
    default_value:
      numerator: 100
      denominator: HUNDRED
  enable_retry_after_header: true

Example configuration
---------------------

Example filter configuration for a globally set rate limiter (e.g.: all vhosts/routes share the same token bucket):

.. literalinclude:: _include/local-rate-limit-global-configuration.yaml
   :language: yaml
   :lines: 13-37
   :linenos:
   :lineno-start: 13
   :caption: :download:`local-rate-limit-global-configuration.yaml <_include/local-rate-limit-global-configuration.yaml>`


Example filter configuration for a globally disabled rate limiter but enabled for a specific route:

.. literalinclude:: _include/local-rate-limit-route-specific-configuration.yaml
   :language: yaml
   :lines: 13-17
   :linenos:
   :lineno-start: 13
   :caption: :download:`local-rate-limit-route-specific-configuration.yaml <_include/local-rate-limit-route-specific-configuration.yaml>`


The route specific configuration:

.. literalinclude:: _include/local-rate-limit-route-specific-configuration.yaml
   :language: yaml
   :lines: 21-53
   :linenos:
   :lineno-start: 21
   :caption: :download:`local-rate-limit-route-specific-configuration.yaml <_include/local-rate-limit-route-specific-configuration.yaml>`


Note that if this filter is configured as globally disabled and there are no virtual host or route level
token buckets, no rate limiting will be applied.

.. _config_http_filters_local_rate_limit_descriptors:

Using rate limit descriptors for local rate limiting
----------------------------------------------------

Rate limit descriptors can be used to override local per-route rate limiting.
A route's :ref:`rate limit action <envoy_v3_api_msg_config.route.v3.RateLimit>`
is used to match up a :ref:`local descriptor
<envoy_v3_api_msg_extensions.common.ratelimit.v3.LocalRateLimitDescriptor>` in
the filter config descriptor list. The local descriptor's token bucket
settings are then used to decide if the request should be rate limited or not
depending on whether the local descriptor's entries match the route's rate
limit actions descriptor entries. If there is no matching descriptor entries,
the default token bucket is used. All the matched local descriptors will be
sorted by tokens per second and try to consume tokens in order, in most cases
if one of them is limited, the remaining descriptors will not consume their tokens.
However, in some cases, it may not work, for example, we have two descriptors
A and B, A is limited 3 requests per second, and B is limited 20 requests per 10 seconds.
Obviously B is stricter than A (token per second), as a result, if we send requests
above 3 in a second, the limited requests from A will also consume tokens of B.
Note that global tokens are not sorted, so we suggest they should be larger than other descriptors.

Example filter configuration using descriptors:

.. literalinclude:: _include/local-rate-limit-with-descriptors.yaml
   :language: yaml
   :lines: 21-81
   :linenos:
   :lineno-start: 21
   :caption: :download:`local-rate-limit-with-descriptors.yaml <_include/local-rate-limit-with-descriptors.yaml>`

In this example, requests are rate-limited for routes prefixed with "/foo" as follows.
If requests come from a downstream service cluster "foo" for "/foo/bar"
path, then 10 req/min are allowed. But if they come from a downstream service
cluster "foo" for "/foo/bar2" path, then 100 req/min are allowed. Otherwise,
1000 req/min are allowed.

Accounting for usage when a stream completes
--------------------------------------------

Set :ref:`apply_on_stream_done <envoy_v3_api_field_config.route.v3.RateLimit.apply_on_stream_done>`
on an embedded ``rate_limits`` action to update local descriptor token buckets when the stream completes.
This allows :ref:`hits_addend <envoy_v3_api_field_config.route.v3.RateLimit.hits_addend>` in the
filter's ``rate_limits`` to use response headers or final stream information, such as
``%RESP(x-usage)%`` or ``%BYTES_SENT%``.

This works in both the filter-level configuration and the per-route configuration in
:ref:`typed_per_filter_config <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>`.
The local rate limit filter ignores ``apply_on_stream_done`` on route and virtual-host
``rate_limits``; those entries continue to apply on the request path.

Example using embedded ``rate_limits``:

.. code-block:: yaml

  rate_limits:
  - actions:
    - generic_key:
        descriptor_key: usage
        descriptor_value: service
  - actions:
    - generic_key:
        descriptor_key: usage
        descriptor_value: service
    apply_on_stream_done: true
    hits_addend:
      format: "%RESP(x-usage)%"

The first action enforces the budget and consumes one token on the request path. The second
charges additional usage reported in the ``x-usage`` response header when the stream completes.
Both actions must match a configured local descriptor. Completion actions update every matching
descriptor bucket without blocking the completed request. Usage exceeding the remaining budget
drains the bucket to zero. The default token bucket is consumed only on the request path.
Missing or empty formatted usage is skipped quietly. Invalid nonempty usage does not generate a
descriptor and logs a warning. Negative hits addends refund tokens using the existing local
descriptor behavior.

With ``local_cluster_rate_limit``, positive charges are divided by the local cluster's token share
factor, while refunds add the specified token count without scaling. This matches request-path
refund behavior. A charge and refund with the same addend do not cancel when the share factor is
less than one.

Request and completion actions share the same bounded cache for wildcard descriptors. A value
first seen at completion creates a bucket and can evict another bucket, including one used for
request enforcement. Revisiting an evicted value creates a fresh bucket with its initial budget.
Size ``max_dynamic_descriptors`` for the combined cardinality of request and completion values.

Completion accounting works with process-wide and per-connection buckets. Request counters and
rate limit response headers reflect the request-path decision; completion accounting does not
change the response or increment request counters again.

As with the global rate limit filter, completion actions also run when the request was rejected
by the rate limit filter. Stream information such as ``%BYTES_SENT%`` includes usage from a local
rate limit response. To account for upstream-reported usage, use a response header such as
``%RESP(x-usage)%`` that is present only when the upstream reports a cost. A missing header is
skipped without logging a warning.

Statistics
----------

The local rate limit filter outputs statistics in the ``<stat_prefix>.http_local_rate_limit.`` namespace.
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
