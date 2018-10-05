.. _config_http_filters_router:

Router
======

The router filter implements HTTP forwarding. It will be used in almost all HTTP proxy scenarios
that Envoy is deployed for. The filter's main job is to follow the instructions specified in the
configured :ref:`route table <envoy_api_msg_RouteConfiguration>`. In addition to forwarding and
redirection, the filter also handles retry, statistics, etc.

* :ref:`v1 API reference <config_http_filters_router_v1>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.router.v2.Router>`

.. _config_http_filters_router_headers_consumed:

HTTP headers (consumed)
-----------------------

The router consumes and sets various HTTP headers both on the egress/request path as well as on the
ingress/response path. They are documented in this section.

.. contents::
  :local:

.. _config_http_filters_router_x-envoy-max-retries:

x-envoy-max-retries
^^^^^^^^^^^^^^^^^^^

If a :ref:`retry policy <envoy_api_field_route.RouteAction.retry_policy>` is in place, Envoy will default to retrying
one time unless explicitly specified. The number of retries can be explicitly set in the route retry config or by using
this header. If a retry policy is not configured and :ref:`config_http_filters_router_x-envoy-retry-on` or
:ref:`config_http_filters_router_x-envoy-retry-grpc-on` headers are not specified, Envoy will not retry a failed
request.

A few notes on how Envoy does retries:

* The route timeout (set via :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` or the
  :ref:`route configuration <envoy_api_field_route.RouteAction.timeout>`) **includes** all
  retries. Thus if the request timeout is set to 3s, and the first request attempt takes 2.7s, the
  retry (including backoff) has .3s to complete. This is by design to avoid an exponential
  retry/timeout explosion.
* Envoy uses a fully jittered exponential backoff algorithm for retries with a base time of 25ms.
  The first retry will be delayed randomly between 0-24ms, the 2nd between 0-74ms, the 3rd between
  0-174ms and so on.
* If max retries is set both by header as well as in the route configuration, the maximum value is
  taken when determining the max retries to use for the request.

.. _config_http_filters_router_x-envoy-retry-on:

x-envoy-retry-on
^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to attempt to retry failed requests (number
of retries defaults to 1 and can be controlled by :ref:`x-envoy-max-retries
<config_http_filters_router_x-envoy-max-retries>` header or the :ref:`route config retry policy
<envoy_api_field_route.RouteAction.retry_policy>`). The value to which the x-envoy-retry-on header is
set indicates the retry policy. One or more policies can be specified using a ',' delimited list.
The supported policies are:

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

connect-failure
  Envoy will attempt a retry if a request is failed because of a connection failure to the upstream
  server (connect timeout, etc.). (Included in *5xx*)

  * **NOTE:** A connection failure/timeout is a the TCP level, not the request level. This does not
    include upstream request timeouts specified via
    :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` or via :ref:`route
    configuration <envoy_api_field_route.RouteAction.retry_policy>`.

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

The number of retries can be controlled via the
:ref:`config_http_filters_router_x-envoy-max-retries` header or via the :ref:`route
configuration <envoy_api_field_route.RouteAction.retry_policy>`.

Note that retry policies can also be applied at the :ref:`route level
<envoy_api_field_route.RouteAction.retry_policy>`.

By default, Envoy will *not* perform retries unless you've configured them per above.

.. _config_http_filters_router_x-envoy-retry-grpc-on:

x-envoy-retry-grpc-on
^^^^^^^^^^^^^^^^^^^^^
Setting this header on egress requests will cause Envoy to attempt to retry failed requests (number of
retries defaults to 1, and can be controlled by
:ref:`x-envoy-max-retries <config_http_filters_router_x-envoy-max-retries>`
header or the :ref:`route config retry policy <envoy_api_field_route.RouteAction.retry_policy>`).
gRPC retries are currently only supported for gRPC status codes in response headers. gRPC status codes in
trailers will not trigger retry logic. One or more policies can be specified  using a ',' delimited
list. The supported policies are:

cancelled
  Envoy will attempt a retry if the gRPC status code in the response headers is "cancelled" (1)

deadline-exceeded
  Envoy will attempt a retry if the gRPC status code in the response headers is "deadline-exceeded" (4)

resource-exhausted
  Envoy will attempt a retry if the gRPC status code in the response headers is "resource-exhausted" (8)

unavailable
  Envoy will attempt a retry if the gRPC status code in the response headers is "unavailable" (14)

As with the x-envoy-retry-grpc-on header, the number of retries can be controlled via the
:ref:`config_http_filters_router_x-envoy-max-retries` header

Note that retry policies can also be applied at the :ref:`route level
<envoy_api_field_route.RouteAction.retry_policy>`.

By default, Envoy will *not* perform retries unless you've configured them per above.

.. _config_http_filters_router_x-envoy-upstream-alt-stat-name:

x-envoy-upstream-alt-stat-name
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to emit upstream response code/timing
statistics to a dual stat tree. This can be useful for application level categories that Envoy
doesn't know about. The output tree is documented :ref:`here <config_cluster_manager_cluster_stats_alt_tree>`.

This should not be confused with :ref:`alt_stat_name <envoy_api_field_Cluster.alt_stat_name>` which
is specified while defining the cluster and when provided specifies an alternative name for the
cluster at the root of the statistic tree.

x-envoy-upstream-canary
^^^^^^^^^^^^^^^^^^^^^^^

If an upstream host sets this header, the router will use it to generate canary specific statistics.
The output tree is documented :ref:`here <config_cluster_manager_cluster_stats_dynamic_http>`.

.. _config_http_filters_router_x-envoy-upstream-rq-timeout-alt-response:

x-envoy-upstream-rq-timeout-alt-response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to set a 204 response code (instead of 504)
in the event of a request timeout. The actual value of the header is ignored; only its presence
is considered. See also :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms`.

.. _config_http_filters_router_x-envoy-upstream-rq-timeout-ms:

x-envoy-upstream-rq-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to override the :ref:`route configuration
<envoy_api_field_route.RouteAction.timeout>`. The timeout must be specified in millisecond
units. See also :ref:`config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms`.

.. _config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms:

x-envoy-upstream-rq-per-try-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to set a *per try* timeout on routed
requests. This timeout must be <= the global route timeout (see
:ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms`) or it is ignored. This allows a
caller to set a tight per try timeout to allow for retries while maintaining a reasonable overall
timeout.

.. _config_http_filters_router_x-envoy-immediate-health-check-fail:

x-envoy-immediate-health-check-fail
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the upstream host returns this header (set to any value), Envoy will immediately assume the
upstream host has failed :ref:`active health checking <arch_overview_health_checking>` (if the
cluster has been :ref:`configured <config_cluster_manager_cluster_hc>` for active health checking).
This can be used to fast fail an upstream host via standard data plane processing without waiting
for the next health check interval. The host can become healthy again via standard active health
checks. See the :ref:`health checking overview <arch_overview_health_checking>` for more
information.

.. _config_http_filters_router_x-envoy-overloaded_consumed:

x-envoy-overloaded
^^^^^^^^^^^^^^^^^^

If this header is set by upstream, Envoy will not retry. Currently the value of the header is not
looked at, only its presence.

.. _config_http_filters_router_x-envoy-decorator-operation:

x-envoy-decorator-operation
^^^^^^^^^^^^^^^^^^^^^^^^^^^

If this header is present on ingress requests, its value will override any locally defined
operation (span) name on the server span generated by the tracing mechanism. Similarly, if
this header is present on an egress response, its value will override any locally defined
operation (span) name on the client span.

.. _config_http_filters_router_headers_set:

HTTP headers (set)
------------------

The router sets various HTTP headers both on the egress/request path as well as on the
ingress/response path. They are documented in this section.

.. contents::
  :local:

.. _config_http_filters_router_x-envoy-attempt-count:

x-envoy-attempt-count
^^^^^^^^^^^^^^^^^^^^^

Sent to the upstream to indicate which attempt the current request is in a series of retries. The value
will be "1" on the initial request, incremeting by one for each retry. Only set if the
:ref:`include_attempt_count_header <envoy_api_field_route.VirtualHost.include_request_attempt_count>`
flag is set to true.

.. _config_http_filters_router_x-envoy-expected-rq-timeout-ms:

x-envoy-expected-rq-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is the time in milliseconds the router expects the request to be completed. Envoy sets this
header so that the upstream host receiving the request can make decisions based on the request
timeout, e.g., early exit. This is set on internal requests and is either taken from the
:ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` header or the :ref:`route timeout
<envoy_api_field_route.RouteAction.timeout>`, in that order.

x-envoy-upstream-service-time
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Contains the time in milliseconds spent by the upstream host processing the request. This is useful
if the client wants to determine service time compared to network latency. This header is set on
responses.

.. _config_http_filters_router_x-envoy-original-path:

x-envoy-original-path
^^^^^^^^^^^^^^^^^^^^^

If the route utilizes :ref:`prefix_rewrite <envoy_api_field_route.RouteAction.prefix_rewrite>`,
Envoy will put the original path header in this header. This can be useful for logging and
debugging.

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
<envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  no_route, Counter, Total requests that had no route and resulted in a 404
  no_cluster, Counter, Total requests in which the target cluster did not exist and resulted in a 404
  rq_redirect, Counter, Total requests that resulted in a redirect response
  rq_direct_response, Counter, Total requests that resulted in a direct response
  rq_total, Counter, Total routed requests

Virtual cluster statistics are output in the
*vhost.<virtual host name>.vcluster.<virtual cluster name>.* namespace and include the following
statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_time, Histogram, Request time milliseconds

Runtime
-------

The router filter supports the following runtime settings:

upstream.base_retry_backoff_ms
  Base exponential retry back off time. See :ref:`here <arch_overview_http_routing_retry>` for more
  information. Defaults to 25ms.

.. _config_http_filters_router_runtime_maintenance_mode:

upstream.maintenance_mode.<cluster name>
  % of requests that will result in an immediate 503 response. This overrides any routing behavior
  for requests that would have been destined for <cluster name>. This can be used for load
  shedding, failure injection, etc. Defaults to disabled.

upstream.use_retry
  % of requests that are eligible for retry. This configuration is checked before any other retry
  configuration and can be used to fully disable retries across all Envoys if needed.
