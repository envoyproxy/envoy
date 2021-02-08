.. _config_http_filters_router:

Router
======

The router filter implements HTTP forwarding. It will be used in almost all HTTP proxy scenarios
that Envoy is deployed for. The filter's main job is to follow the instructions specified in the
configured :ref:`route table <envoy_v3_api_msg_config.route.v3.RouteConfiguration>`. In addition to forwarding and
redirection, the filter also handles retry, statistics, etc.

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.router.v3.Router>`
* This filter should be configured with the name *envoy.filters.http.router*.

.. _config_http_filters_router_headers_consumed:

HTTP headers (consumed from downstreams)
----------------------------------------

The router consumes and sets various HTTP headers both on the egress/request path as well as on the
ingress/response path. They are documented in this section.

.. contents::
  :local:

.. _config_http_filters_router_x-envoy-max-retries:

x-envoy-max-retries
^^^^^^^^^^^^^^^^^^^
If a :ref:`route config retry policy <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` or a
:ref:`virtual host retry policy <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>` is in place, Envoy will default to retrying
one time unless explicitly specified. The number of retries can be explicitly set in the virtual host retry config,
the route retry config, or by using this header. If this header is used, its value takes precedence over the number of
retries set in either retry policy. If a retry policy is not configured and :ref:`config_http_filters_router_x-envoy-retry-on`
or :ref:`config_http_filters_router_x-envoy-retry-grpc-on` headers are not specified, Envoy will not retry a failed request.

A few notes on how Envoy does retries:

* The route timeout (set via :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` or the
  :ref:`timeout <envoy_v3_api_field_config.route.v3.RouteAction.timeout>` in route configuration or set via
  `grpc-timeout header <https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_  by specifying
  :ref:`max_grpc_timeout <envoy_v3_api_field_config.route.v3.RouteAction.timeout>` in route configuration) **includes** all
  retries. Thus if the request timeout is set to 3s, and the first request attempt takes 2.7s, the
  retry (including back-off) has .3s to complete. This is by design to avoid an exponential
  retry/timeout explosion.
* By default, Envoy uses a fully jittered exponential back-off algorithm for retries with a default base
  interval of 25ms. Given a base interval B and retry number N, the back-off for the retry is in
  the range :math:`\big[0, (2^N-1)B\big)`. For example, given the default interval, the first retry
  will be delayed randomly by 0-24ms, the 2nd by 0-74ms, the 3rd by 0-174ms, and so on. The
  interval is capped at a maximum interval, which defaults to 10 times the base interval (250ms).
  The default base interval (and therefore the maximum interval) can be manipulated by setting the
  upstream.base_retry_backoff_ms runtime parameter. The back-off intervals can also be modified
  by configuring the retry policy's
  :ref:`retry back-off <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_back_off>`.
* Envoy can also be configured to use feedback from the upstream server to decide the interval between
  retries. Response headers like ``Retry-After`` or ``X-RateLimit-Reset`` instruct the client how long
  to wait before re-trying. The retry policy's
  :ref:`rate limited retry back off <envoy_v3_api_field_config.route.v3.RetryPolicy.rate_limited_retry_back_off>`
  strategy can be configured to expect a particular header, and if that header is present in the response Envoy
  will use its value to decide the back-off. If the header is not present, or if it cannot be parsed
  successfully, Envoy will use the default exponential back-off algorithm instead.

.. _config_http_filters_router_x-envoy-retry-on:

x-envoy-retry-on
^^^^^^^^^^^^^^^^

Setting this header will cause Envoy to attempt to retry failed requests (number
of retries defaults to 1 and can be controlled by :ref:`x-envoy-max-retries
<config_http_filters_router_x-envoy-max-retries>` header or the :ref:`route config retry policy
<envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` or the :ref:`virtual host retry policy <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>`).
The value to which the x-envoy-retry-on header is set indicates the retry policy. One or more policies
can be specified using a ',' delimited list. The supported policies are:

5xx
  Envoy will attempt a retry if the upstream server responds with any 5xx response code, or does not
  respond at all (disconnect/reset/read timeout). (Includes *connect-failure* and *refused-stream*)

  * **NOTE:** Envoy will not retry when a request exceeds
    :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` (resulting in a 504 error
    code). Use :ref:`config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms` if you want
    to retry when individual attempts take too long.
    :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` is an outer time limit for a
    request, including any retries that take place.

gateway-error
  This policy is similar to the *5xx* policy but will only retry requests that result in a 502, 503,
  or 504.

reset
  Envoy will attempt a retry if the upstream server does not respond at all (disconnect/reset/read timeout.)

connect-failure
  Envoy will attempt a retry if a request is failed because of a connection failure to the upstream
  server (connect timeout, etc.). (Included in *5xx*)

  * **NOTE:** A connection failure/timeout is a the TCP level, not the request level. This does not
    include upstream request timeouts specified via
    :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` or via :ref:`route
    configuration <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` or via
    :ref:`virtual host retry policy <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>`.

.. _config_http_filters_router_retry_policy-envoy-ratelimited:

envoy-ratelimited
  Envoy will retry if the header :ref:`x-envoy-ratelimited<config_http_filters_router_x-envoy-ratelimited>`
  is present.

retriable-4xx
  Envoy will attempt a retry if the upstream server responds with a retriable 4xx response code.
  Currently, the only response code in this category is 409.

  * **NOTE:** Be careful turning on this retry type. There are certain cases where a 409 can indicate
    that an optimistic locking revision needs to be updated. Thus, the caller should not retry and
    needs to read then attempt another write. If a retry happens in this type of case it will always
    fail with another 409.

refused-stream
  Envoy will attempt a retry if the upstream server resets the stream with a REFUSED_STREAM error
  code. This reset type indicates that a request is safe to retry. (Included in *5xx*)

retriable-status-codes
  Envoy will attempt a retry if the upstream server responds with any response code matching one defined
  in either :ref:`the retry policy <envoy_v3_api_field_config.route.v3.RetryPolicy.retriable_status_codes>`
  or in the :ref:`config_http_filters_router_x-envoy-retriable-status-codes` header.

retriable-headers
  Envoy will attempt a retry if the upstream server response includes any headers matching in either
  :ref:`the retry policy <envoy_v3_api_field_config.route.v3.RetryPolicy.retriable_headers>` or in the
  :ref:`config_http_filters_router_x-envoy-retriable-header-names` header.

The number of retries can be controlled via the
:ref:`config_http_filters_router_x-envoy-max-retries` header or via the :ref:`route
configuration <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` or via the
:ref:`virtual host retry policy <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>`.

Note that retry policies can also be applied at the :ref:`route level
<envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` or the
:ref:`virtual host level <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>`.

By default, Envoy will *not* perform retries unless you've configured them per above.

.. _config_http_filters_router_x-envoy-retry-grpc-on:

x-envoy-retry-grpc-on
^^^^^^^^^^^^^^^^^^^^^
Setting this header will cause Envoy to attempt to retry failed requests (number of retries defaults
to 1, and can be controlled by :ref:`x-envoy-max-retries <config_http_filters_router_x-envoy-max-retries>`
header or the :ref:`route config retry policy <envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>`) or the
:ref:`virtual host retry policy <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>`.
gRPC retries are currently only supported for gRPC status codes in response headers. gRPC status codes in
trailers will not trigger retry logic. One or more policies can be specified  using a ',' delimited
list. The supported policies are:

cancelled
  Envoy will attempt a retry if the gRPC status code in the response headers is "cancelled" (1)

deadline-exceeded
  Envoy will attempt a retry if the gRPC status code in the response headers is "deadline-exceeded" (4)

internal
  Envoy will attempt to retry if the gRPC status code in the response headers is "internal" (13)

resource-exhausted
  Envoy will attempt a retry if the gRPC status code in the response headers is "resource-exhausted" (8)

unavailable
  Envoy will attempt a retry if the gRPC status code in the response headers is "unavailable" (14)

As with the x-envoy-retry-grpc-on header, the number of retries can be controlled via the
:ref:`config_http_filters_router_x-envoy-max-retries` header

Note that retry policies can also be applied at the :ref:`route level
<envoy_v3_api_field_config.route.v3.RouteAction.retry_policy>` or the
:ref:`virtual host level <envoy_v3_api_field_config.route.v3.VirtualHost.retry_policy>`.

By default, Envoy will *not* perform retries unless you've configured them per above.

.. _config_http_filters_router_x-envoy-retriable-header-names:

x-envoy-retriable-header-names
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Setting this header informs Envoy about what response headers should be considered retriable. It is used
in conjunction with the :ref:`retriable-headers <config_http_filters_router_x-envoy-retry-on>` retry policy.
When the corresponding retry policy is set, the response headers provided by this list header value will be
considered retriable in addition to the response headers enabled for retry through other retry policies.

The list is a comma-separated list of header names: "X-Upstream-Retry,X-Try-Again" would cause any upstream
responses containing either one of the specified headers to be retriable if 'retriable-headers' retry policy
is enabled. Header names are case-insensitive.

Only the names of retriable response headers can be specified via the request header. A more sophisticated
retry policy based on the response headers can be specified by using arbitrary header matching rules
via :ref:`retry policy configuration <envoy_v3_api_field_config.route.v3.RetryPolicy.retriable_headers>`.

This header will only be honored for requests from internal clients.

.. _config_http_filters_router_x-envoy-retriable-status-codes:

x-envoy-retriable-status-codes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Setting this header informs Envoy about what status codes should be considered retriable when used in
conjunction with the :ref:`retriable-status-code <config_http_filters_router_x-envoy-retry-on>` retry policy.
When the corresponding retry policy is set, the list of retriable status codes will be considered retriable
in addition to the status codes enabled for retry through other retry policies.

The list is a comma delimited list of integers: "409" would cause 409 to be considered retriable, while "504,409"
would consider both 504 and 409 retriable.

This header will only be honored for requests from internal clients.

.. _config_http_filters_router_x-envoy-upstream-alt-stat-name:

x-envoy-upstream-alt-stat-name
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header will cause Envoy to emit upstream response code/timing statistics to a dual stat tree.
This can be useful for application level categories that Envoy doesn't know about. The output tree
is documented :ref:`here <config_cluster_manager_cluster_stats_alt_tree>`.

This should not be confused with :ref:`alt_stat_name <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` which
is specified while defining the cluster and when provided specifies an alternative name for the
cluster at the root of the statistic tree.

.. _config_http_filters_router_x-envoy-upstream-rq-timeout-alt-response:

x-envoy-upstream-rq-timeout-alt-response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header will cause Envoy to set a 204 response code (instead of 504) in the event of a request timeout.
The actual value of the header is ignored; only its presence is considered. See also
:ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms`.

.. _config_http_filters_router_x-envoy-upstream-rq-timeout-ms:

x-envoy-upstream-rq-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header will cause Envoy to override the :ref:`route configuration timeout
<envoy_v3_api_field_config.route.v3.RouteAction.timeout>` or gRPC client timeout set via `grpc-timeout header
<https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md>`_  by specifying :ref:`max_grpc_timeout
<envoy_v3_api_field_config.route.v3.RouteAction.timeout>`. The timeout must be specified in millisecond
units. See also :ref:`config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms`.

.. _config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms:

x-envoy-upstream-rq-per-try-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header will cause Envoy to set a *per try* timeout on routed requests.
If a global route timeout is configured, this timeout must be less than the global route
timeout (see :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms`) or it is ignored.
This allows a caller to set a tight per try timeout to allow for retries while maintaining a
reasonable overall timeout. This timeout only applies before any part of the response is sent to
the downstream, which normally happens after the upstream has sent response headers.

x-envoy-hedge-on-per-try-timeout
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header will cause Envoy to use a request hedging strategy in the case of a per try timeout.
This overrides the value set in the :ref:`route configuration
<envoy_v3_api_field_config.route.v3.HedgePolicy.hedge_on_per_try_timeout>`. This means that a retry
will be issued without resetting the original request, leaving multiple upstream requests
in flight.

The value of the header should be "true" or "false", and is ignored if invalid.

.. _config_http_filters_router_x-envoy-decorator-operation:

x-envoy-decorator-operation
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The value of this header will override any locally defined operation (span) name on the
server span generated by the tracing mechanism.

HTTP response headers consumed from upstream
--------------------------------------------

x-envoy-decorator-operation
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The value of this header will override any locally defined operation (span) name on the
client span generated by the tracing mechanism.

x-envoy-upstream-canary
^^^^^^^^^^^^^^^^^^^^^^^

If an upstream host sets this header, the router will use it to generate canary specific statistics.
The output tree is documented :ref:`here <config_cluster_manager_cluster_stats_dynamic_http>`.

.. _config_http_filters_router_x-envoy-immediate-health-check-fail:

x-envoy-immediate-health-check-fail
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the upstream host returns this header (set to any value), Envoy will immediately assume the
upstream host has failed :ref:`active health checking <arch_overview_health_checking>` (if the
cluster has been :ref:`configured <config_cluster_manager_cluster_hc>` for active health checking)
and :ref:`exclude <arch_overview_load_balancing_excluded>` it from load balancing. This can be used
to fast fail an upstream host via standard data plane processing without waiting for the next health
check interval. The host can become healthy again via standard active health checks. See the
:ref:`active health checking fast failure overview <arch_overview_health_checking_fast_failure>` for
more information.

.. _config_http_filters_router_x-envoy-ratelimited:

x-envoy-ratelimited
^^^^^^^^^^^^^^^^^^^

If this header is set by upstream, Envoy will not retry unless the retry policy
:ref:`envoy-ratelimited<config_http_filters_router_retry_policy-envoy-ratelimited>`
is enabled. Currently, the value of the header is not looked at, only its
presence. This header is set by :ref:`rate limit
filter<config_http_filters_rate_limit>` when the request is rate limited.

.. _config_http_filters_router_headers_set:

HTTP request headers set on upstream calls
------------------------------------------

The router sets various HTTP headers both on the egress/request path as well as on the
ingress/response path. They are documented in this section.

.. contents::
  :local:

.. _config_http_filters_router_x-envoy-attempt-count:

x-envoy-attempt-count
^^^^^^^^^^^^^^^^^^^^^

Sent to the upstream to indicate which attempt the current request is in a series of retries. The value
will be "1" on the initial request, incrementing by one for each retry. Only set if the
:ref:`include_request_attempt_count <envoy_v3_api_field_config.route.v3.VirtualHost.include_request_attempt_count>`
flag is set to true.

Sent to the downstream to indicate how many upstream requests took place. The header will be absent if
the router did not send any upstream requests. The value will be "1" if only the original upstream
request was sent, incrementing by one for each retry. Only set if the
:ref:`include_attempt_count_in_response <envoy_v3_api_field_config.route.v3.VirtualHost.include_attempt_count_in_response>`
flag is set to true.

.. _config_http_filters_router_x-envoy-expected-rq-timeout-ms:

x-envoy-expected-rq-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is the time in milliseconds the router expects the request to be completed. Envoy sets this
header so that the upstream host receiving the request can make decisions based on the request
timeout, e.g., early exit. This is set on internal requests and is either taken from the
:ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` header or the :ref:`route timeout
<envoy_v3_api_field_config.route.v3.RouteAction.timeout>`, in that order.

.. _config_http_filters_router_x-envoy-original-path:

x-envoy-original-path
^^^^^^^^^^^^^^^^^^^^^

If the route utilizes :ref:`prefix_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.prefix_rewrite>`
or :ref:`regex_rewrite <envoy_v3_api_field_config.route.v3.RouteAction.regex_rewrite>`,
Envoy will put the original path header in this header. This can be useful for logging and
debugging.

HTTP response headers set on downstream responses
-------------------------------------------------

.. _config_http_filters_router_x-envoy-upstream-service-time:

x-envoy-upstream-service-time
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Contains the time in milliseconds spent by the upstream host processing the request and the network
latency between Envoy and upstream host. This is useful if the client wants to determine service time
compared to network latency between client and Envoy. This header is set on responses.

.. _config_http_filters_router_x-envoy-overloaded_set:

x-envoy-overloaded
^^^^^^^^^^^^^^^^^^

Envoy will set this header on the downstream response
if a request was dropped due to either :ref:`maintenance mode
<config_http_filters_router_runtime_maintenance_mode>` or upstream :ref:`circuit breaking
<arch_overview_circuit_break>`.

.. _config_http_filters_router_stats:

Statistics
----------

The router outputs many statistics in the cluster namespace (depending on the cluster specified in
the chosen route). See :ref:`here <config_cluster_manager_cluster_stats>` for more information.

The router filter outputs statistics in the *http.<stat_prefix>.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  no_route, Counter, Total requests that had no route and resulted in a 404
  no_cluster, Counter, Total requests in which the target cluster did not exist and which by default result in a 503
  rq_redirect, Counter, Total requests that resulted in a redirect response
  rq_direct_response, Counter, Total requests that resulted in a direct response
  rq_total, Counter, Total routed requests
  rq_reset_after_downstream_response_started, Counter, Total requests that were reset after downstream response had started

.. _config_http_filters_router_vcluster_stats:

Virtual Clusters
^^^^^^^^^^^^^^^^

Virtual cluster statistics are output in the
*vhost.<virtual host name>.vcluster.<virtual cluster name>.* namespace and include the following
statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_retry, Counter, Total request retries
  upstream_rq_retry_limit_exceeded, Counter, Total requests not retried due to exceeding :ref:`the configured number of maximum retries <config_http_filters_router_x-envoy-max-retries>`
  upstream_rq_retry_overflow, Counter, Total requests not retried due to circuit breaking or exceeding the :ref:`retry budgets <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.retry_budget>`
  upstream_rq_retry_success, Counter, Total request retry successes
  upstream_rq_time, Histogram, Request time milliseconds
  upstream_rq_timeout, Counter, Total requests that timed out waiting for a response
  upstream_rq_total, Counter, Total requests initiated by the router to the upstream

Runtime
-------

The router filter supports the following runtime settings:

upstream.base_retry_backoff_ms
  Base exponential retry back-off time. See :ref:`here <arch_overview_http_routing_retry>` and
  :ref:`config_http_filters_router_x-envoy-max-retries` for more information. Defaults to 25ms.
  The default maximum retry back-off time is 10 times this value.

.. _config_http_filters_router_runtime_maintenance_mode:

upstream.maintenance_mode.<cluster name>
  % of requests that will result in an immediate 503 response. This overrides any routing behavior
  for requests that would have been destined for <cluster name>. This can be used for load
  shedding, failure injection, etc. Defaults to disabled.

upstream.use_retry
  % of requests that are eligible for retry. This configuration is checked before any other retry
  configuration and can be used to fully disable retries across all Envoys if needed.
