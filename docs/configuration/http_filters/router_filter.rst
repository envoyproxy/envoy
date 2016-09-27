.. _config_http_filters_router:

Router
======

The router filter implements HTTP forwarding. It will be used in almost all HTTP proxy scenarios
that Envoy is deployed for. The filter's main job is to follow the instructions specified in the
configured :ref:`route table <config_http_conn_man_route_table>`. In addition to forwarding and
redirection, the filter also handles retry, statistics, etc.

.. code-block:: json

  {
    "type": "decoder",
    "name": "router",
    "config": {}
  }

.. _config_http_filters_router_headers:

HTTP headers
------------

The router responds to various HTTP headers both on the egress/request path as well as on the
ingress/response path. They are documented in this section.

.. contents::
  :local:

x-envoy-expected-rq-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This is the time in milliseconds the router expects the request to be completed. Envoy sets this
header so that the upstream host receiving the request can make decisions based on the request
timeout, e.g., early exit. This is set on internal requests and is either taken from the
:ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` header or the :ref:`route timeout
<config_http_conn_man_route_table_route_timeout>`, in that order.

.. _config_http_filters_router_x-envoy-max-retries:

x-envoy-max-retries
^^^^^^^^^^^^^^^^^^^

If a retry policy is in place, setting this header controls the number of retries that Envoy will
perform on the request's behalf. If the header is not specified and there is no :ref:`route
configuration <config_http_conn_man_route_table_route_retry>` Envoy will perform a single retry. A
few notes on how Envoy does retries:

* The route timeout (set via :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` or the
  :ref:`route configuration <config_http_conn_man_route_table_route_timeout>`) **includes** all
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

Setting this header on egress requests will cause Envoy to attempt to retry failed requests. The
value that the header is set to indicates the retry policy. One or more policies can be specified
using a ',' delimited list. The supported policies are:

5xx
  Envoy will attempt a retry if the upstream server responds with any 5xx response code, or does not
  respond at all (disconnect/reset/etc.). (Includes *connect-failure* and *refused-stream*)

  * **NOTE:** Envoy will not retry when a request exceeds :ref:`x-envoy-upstream-rq-timeout-ms` (resulting in a 504 error code). Use :ref:`x-envoy-upstream-rq-per-try-timeout-ms` if you want to retry when individual attempts take too long. :ref:`x-envoy-upstream-rq-timeout-ms` is an outer time limit for a request, including any retries that take place.

connect-failure
  Envoy will attempt a retry if a request is failed because of a connection failure to the upstream
  server (connect timeout, etc.). (Included in *5xx*)

  * **NOTE:** A connection failure/timeout is a the TCP level, not the request level. This does not
    include upstream request timeouts specified via
    :ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms` or via :ref:`route
    configuration <config_http_conn_man_route_table_route_retry>`.

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
configuration <config_http_conn_man_route_table_route_retry>`.

Note that retry policies can also be applied at the :ref:`route level
<config_http_conn_man_route_table_route_retry>`.

By default, Envoy will *not* perform retries unless you've configured them per above.

x-envoy-upstream-alt-stat-name
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to emit upstream response code/timing
statistics to a dual stat tree. This can be useful for application level categories that Envoy
doesn't know about. The output tree is documented :ref:`here
<config_cluster_manager_cluster_stats_alt_tree>`.

x-envoy-upstream-canary
^^^^^^^^^^^^^^^^^^^^^^^

If an upstream host sets this header, the router will use it to generate canary specific statistics.
The output tree is documented :ref:`here <config_cluster_manager_cluster_stats_dynamic_http>`.

.. _config_http_filters_router_x-envoy-upstream-rq-timeout-ms:

x-envoy-upstream-rq-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to override the :ref:`route configuration
<config_http_conn_man_route_table_route_timeout>`. The timeout must be specified in millisecond
units. See also :ref:`config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms`.

.. _config_http_filters_router_x-envoy-upstream-rq-per-try-timeout-ms:

x-envoy-upstream-rq-per-try-timeout-ms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Setting this header on egress requests will cause Envoy to set a *per try* timeout on routed
requests. This timeout must be <= the global route timeout (see
:ref:`config_http_filters_router_x-envoy-upstream-rq-timeout-ms`) or it is ignored. This allows a
caller to set a tight per try timeout to allow for retries while maintaining a reasonable overall
timeout.

x-envoy-upstream-service-time
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Contains the time in milliseconds spent by the upstream host processing the request. This is useful
if the client wants to determine service time compared to network latency. This header is set on
responses.

.. _config_http_filters_router_stats:

Statistics
----------

The router outputs many statistics in the cluster namespace (depending on the cluster specified in
the chosen route). See :ref:`here <config_cluster_manager_cluster_stats>` for more information.

The router filter outputs statistics in the *http.<stat_prefix>.* namespace. The :ref:`stat
prefix <config_http_conn_man_stat_prefix>` comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  no_route, Counter, Total requests that had no route and resulted in a 404
  rq_redirect, Counter, Total requests that resulted in a redirect response
  rq_total, Counter, Total routed requests

Virtual cluster statistics are output in the
*vhost.<virtual host name>.vcluster.<virtual cluster name>.* namespace and include the following
statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  upstream_rq_<\*xx>, Counter, "Aggregate HTTP response codes (e.g., 2xx, 3xx, etc.)"
  upstream_rq_<\*>, Counter, "Specific HTTP response codes (e.g., 201, 302, etc.)"
  upstream_rq_time, Timer, Request time milliseconds

Runtime
-------

The router filter supports the following runtime settings:

upstream.base_retry_backoff_ms
  Base exponential retry back off time. See :ref:`here <arch_overview_http_routing_retry>` for more
  information. Defaults to 25ms.

upstream.maintenance_mode.<cluster name>
  % of requests that will result in an immediate 503 response. This overrides any routing behavior
  for requests that would have been destined for <cluster name>. This can be used for load
  shedding, failure injection, etc. Defaults to disabled.

upstream.use_retry
  % of requests that are eligible for retry. This configuration is checked before any other retry
  configuration and can be used to fully disable retries across all Envoys if needed.
