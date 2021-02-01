.. _arch_overview_health_checking:

Health checking
===============

Active health checking can be :ref:`configured <config_cluster_manager_cluster_hc>` on a per
upstream cluster basis. As described in the :ref:`service discovery
<arch_overview_service_discovery>` section, active health checking and the EDS service discovery
type go hand in hand. However, there are other scenarios where active health checking is desired
even when using the other service discovery types. Envoy supports three different types of health
checking along with various settings (check interval, failures required before marking a host
unhealthy, successes required before marking a host healthy, etc.):

* **HTTP**: During HTTP health checking Envoy will send an HTTP request to the upstream host. By
  default, it expects a 200 response if the host is healthy. Expected response codes are
  :ref:`configurable <envoy_v3_api_msg_config.core.v3.HealthCheck.HttpHealthCheck>`. The
  upstream host can return 503 if it wants to immediately notify downstream hosts to no longer
  forward traffic to it.
* **L3/L4**: During L3/L4 health checking, Envoy will send a configurable byte buffer to the
  upstream host. It expects the byte buffer to be echoed in the response if the host is to be
  considered healthy. Envoy also supports connect only L3/L4 health checking.
* **Redis**: Envoy will send a Redis PING command and expect a PONG response. The upstream Redis
  server can respond with anything other than PONG to cause an immediate active health check
  failure. Optionally, Envoy can perform EXISTS on a user-specified key. If the key does not exist
  it is considered a passing healthcheck. This allows the user to mark a Redis instance for
  maintenance by setting the specified key to any value and waiting for traffic to drain. See
  :ref:`redis_key <envoy_v3_api_msg_config.health_checker.redis.v2.Redis>`.

Health checks occur over the transport socket specified for the cluster. This implies that if a cluster is
using a TLS-enabled transport socket, the health check will also occur over TLS. The
:ref:`TLS options <envoy_v3_api_msg_config.core.v3.HealthCheck.TlsOptions>` used for health check connections
can be specified, which is useful if the corresponding upstream is using ALPN-based
:ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>` with different protocols for
health checks versus data connections.

.. _arch_overview_per_cluster_health_check_config:

Per cluster member health check config
--------------------------------------

If active health checking is configured for an upstream cluster, a specific additional configuration
for each registered member can be specified by setting the
:ref:`HealthCheckConfig<envoy_v3_api_msg_config.endpoint.v3.Endpoint.HealthCheckConfig>`
in the :ref:`Endpoint<envoy_v3_api_msg_config.endpoint.v3.Endpoint>` of an :ref:`LbEndpoint<envoy_v3_api_msg_config.endpoint.v3.LbEndpoint>`
of each defined :ref:`LocalityLbEndpoints<envoy_v3_api_msg_config.endpoint.v3.LocalityLbEndpoints>` in a
:ref:`ClusterLoadAssignment<envoy_v3_api_msg_config.endpoint.v3.ClusterLoadAssignment>`.

An example of setting up :ref:`health check config<envoy_v3_api_msg_config.endpoint.v3.Endpoint.HealthCheckConfig>`
to set a :ref:`cluster member<envoy_v3_api_msg_config.endpoint.v3.Endpoint>`'s alternative health check
:ref:`port<envoy_v3_api_field_config.endpoint.v3.Endpoint.HealthCheckConfig.port_value>` is:

.. code-block:: yaml

  load_assignment:
    endpoints:
    - lb_endpoints:
      - endpoint:
          health_check_config:
            port_value: 8080
          address:
            socket_address:
              address: localhost
              port_value: 80

.. _arch_overview_health_check_logging:

Health check event logging
--------------------------

A per-healthchecker log of ejection and addition events can optionally be produced by Envoy by
specifying a log file path in :ref:`the HealthCheck config <envoy_v3_api_field_config.core.v3.HealthCheck.event_log_path>`.
The log is structured as JSON dumps of
:ref:`HealthCheckEvent messages <envoy_v3_api_msg_data.core.v3.HealthCheckEvent>`.

Envoy can be configured to log all health check failure events by setting the :ref:`always_log_health_check_failures
flag <envoy_v3_api_field_config.core.v3.HealthCheck.always_log_health_check_failures>` to true.

Passive health checking
-----------------------

Envoy also supports passive health checking via :ref:`outlier detection
<arch_overview_outlier_detection>`.

Connection pool interactions
----------------------------

See :ref:`here <arch_overview_conn_pool_health_checking>` for more information.

.. _arch_overview_health_checking_filter:

HTTP health checking filter
---------------------------

When an Envoy mesh is deployed with active health checking between clusters, a large amount of
health checking traffic can be generated. Envoy includes an HTTP health checking filter that can be
installed in a configured HTTP listener. This filter is capable of a few different modes of
operation:

* **No pass through**: In this mode, the health check request is never passed to the local service.
  Envoy will respond with a 200 or a 503 depending on the current draining state of the server.
* **No pass through, computed from upstream cluster health**: In this mode, the health checking
  filter will return a 200 or a 503 depending on whether at least a :ref:`specified percentage
  <envoy_v3_api_field_extensions.filters.http.health_check.v3.HealthCheck.cluster_min_healthy_percentages>`
  of the servers are available (healthy + degraded) in one or more upstream clusters. (If the Envoy
  server is in a draining state, though, it will respond with a 503 regardless of the upstream
  cluster health.)
* **Pass through**: In this mode, Envoy will pass every health check request to the local service.
  The service is expected to return a 200 or a 503 depending on its health state.
* **Pass through with caching**: In this mode, Envoy will pass health check requests to the local
  service, but then cache the result for some period of time. Subsequent health check requests will
  return the cached value up to the cache time. When the cache time is reached, the next health
  check request will be passed to the local service. This is the recommended mode of operation when
  operating a large mesh. Envoy uses persistent connections for health checking traffic and health
  check requests have very little cost to Envoy itself. Thus, this mode of operation yields an
  eventually consistent view of the health state of each upstream host without overwhelming the
  local service with a large number of health check requests.

Further reading:

* Health check filter :ref:`configuration <config_http_filters_health_check>`.
* :ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>` admin endpoint.
* :ref:`/healthcheck/ok <operations_admin_interface_healthcheck_ok>` admin endpoint.

.. _arch_overview_health_checking_fast_failure:

Active health checking fast failure
-----------------------------------

When using active health checking along with passive health checking (:ref:`outlier detection
<arch_overview_outlier_detection>`), it is common to use a long health checking interval to avoid a
large amount of active health checking traffic. In this case, it is still useful to be able to
quickly drain an upstream host when using the :ref:`/healthcheck/fail
<operations_admin_interface_healthcheck_fail>` admin endpoint. To support this, the :ref:`router
filter <config_http_filters_router>` *and* the HTTP active health checker will respond to the
:ref:`x-envoy-immediate-health-check-fail
<config_http_filters_router_x-envoy-immediate-health-check-fail>` header. If this header is set by
an upstream host, Envoy will immediately mark the host as being failed for active health check and
:ref:`excluded <arch_overview_load_balancing_excluded>` from load balancing. Note that this only
occurs if the host's cluster has active health checking :ref:`configured
<config_cluster_manager_cluster_hc>`. The :ref:`health checking filter
<config_http_filters_health_check>` will automatically set this header if Envoy has been marked as
failed via the :ref:`/healthcheck/fail <operations_admin_interface_healthcheck_fail>` admin
endpoint.

.. _arch_overview_health_checking_identity:

Health check identity
---------------------

Just verifying that an upstream host responds to a particular health check URL does not necessarily
mean that the upstream host is valid. For example, when using eventually consistent service
discovery in a cloud auto scaling or container environment, it's possible for a host to go away and
then come back with the same IP address, but as a different host type. One solution to this problem
is having a different HTTP health checking URL for every service type. The downside of that approach
is that overall configuration becomes more complicated as every health check URL is fully custom.

The Envoy HTTP health checker supports the :ref:`service_name_matcher
<envoy_v3_api_field_config.core.v3.HealthCheck.HttpHealthCheck.service_name_matcher>` option. If this option is set,
the health checker additionally compares the value of the *x-envoy-upstream-healthchecked-cluster*
response header to *service_name_matcher*. If the values do not match, the health check does not pass.
The upstream health check filter appends *x-envoy-upstream-healthchecked-cluster* to the response headers.
The appended value is determined by the :option:`--service-cluster` command line option.

.. _arch_overview_health_checking_degraded:

Degraded health
---------------
When using the HTTP health checker, an upstream host can return ``x-envoy-degraded`` to inform the
health checker that the host is degraded. See :ref:`here <arch_overview_load_balancing_degraded>` for
how this affects load balancing.


