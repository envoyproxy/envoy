.. _arch_overview_circuit_break:

Circuit breaking
================

Circuit breaking is a critical component of distributed systems. It’s nearly always better to fail
quickly and apply back pressure downstream as soon as possible. One of the main benefits of an Envoy
mesh is that Envoy enforces circuit breaking limits at the network level as opposed to having to
configure and code each application independently. Envoy supports various types of fully distributed
(not coordinated) circuit breaking:

.. _arch_overview_circuit_break_cluster_maximum_connections:

* **Cluster maximum connections**: The maximum number of connections that Envoy will establish to
  all hosts in an upstream cluster. If this circuit breaker overflows the :ref:`upstream_cx_overflow
  <config_cluster_manager_cluster_stats>` counter for the cluster will increment.
* **Cluster maximum pending requests**: The maximum number of requests that will be queued while
  waiting for a ready connection pool connection. Requests are added to the list
  of pending requests whenever there aren't enough upstream connections available to immediately dispatch
  the request. For HTTP/2 connections, if :ref:`max concurrent streams <envoy_api_field_core.Http2ProtocolOptions.max_concurrent_streams>`
  and :ref:`max requests per connection <envoy_api_field_Cluster.max_requests_per_connection>` are not
  configured, all requests will be multiplexed over the same connection so this circuit breaker
  will only be hit when no connection is already established. If this circuit breaker overflows the
  :ref:`upstream_rq_pending_overflow <config_cluster_manager_cluster_stats>` counter for the cluster will
  increment.
* **Cluster maximum requests**: The maximum number of requests that can be outstanding to all hosts
  in a cluster at any given time. If this circuit breaker overflows the :ref:`upstream_rq_pending_overflow <config_cluster_manager_cluster_stats>`
  counter for the cluster will increment.
* **Cluster maximum active retries**: The maximum number of retries that can be outstanding to all
  hosts in a cluster at any given time. In general we recommend using :ref:`retry budgets <envoy_api_field_cluster.CircuitBreakers.Thresholds.retry_budget>`; however, if static circuit breaking is preferred it should aggressively circuit break
  retries. This is so that retries for sporadic failures are allowed, but the overall retry volume cannot
  explode and cause large scale cascading failure. If this circuit breaker overflows the
  :ref:`upstream_rq_retry_overflow <config_cluster_manager_cluster_stats>` counter for the cluster
  will increment.

  .. _arch_overview_circuit_break_cluster_maximum_connection_pools:

* **Cluster maximum concurrent connection pools**: The maximum number of connection pools that can be
  concurrently instantiated. Some features, such as the
  :ref:`Original Src Listener Filter <arch_overview_ip_transparency_original_src_listener>`, can
  create an unbounded number of connection pools. When a cluster has exhausted its concurrent
  connection pools, it will attempt to reclaim an idle one. If it cannot, then the circuit breaker
  will overflow. This differs from
  :ref:`Cluster maximum connections <arch_overview_circuit_break_cluster_maximum_connections>` in that
  connection pools never time out, whereas connections typically will. Connections automatically
  clean up; connection pools do not. Note that in order for a connection pool to function it needs
  at least one upstream connection, so this value should likely be no greater than
  :ref:`Cluster maximum connections <arch_overview_circuit_break_cluster_maximum_connections>`.
  If this circuit breaker overflows the
  :ref:`upstream_cx_pool_overflow <config_cluster_manager_cluster_stats>` counter for the cluster
  will increment.


Each circuit breaking limit is :ref:`configurable <config_cluster_manager_cluster_circuit_breakers>`
and tracked on a per upstream cluster and per priority basis. This allows different components of
the distributed system to be tuned independently and have different limits. The live state of these
circuit breakers, including the number of resources remaining until a circuit breaker opens, can
be observed via :ref:`statistics <config_cluster_manager_cluster_stats_circuit_breakers>`.

Note that circuit breaking will cause the :ref:`x-envoy-overloaded
<config_http_filters_router_x-envoy-overloaded_set>` header to be set by the router filter in the
case of HTTP requests.
