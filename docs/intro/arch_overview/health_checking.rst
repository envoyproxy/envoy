.. _arch_overview_health_checking:

Health checking
===============

Active health checking can be :ref:`configured <config_cluster_manager_cluster_hc>` on a per
upstream cluster basis. As described in the :ref:`service discovery
<arch_overview_service_discovery>` section, active health checking and the SDS service discovery
type go hand in hand. However, there are other scenarios where active health checking is desired
even when using the other service discovery types. Envoy supports three different types of health
checking along with various settings (check interval, failures required before marking a host
unhealthy, successes required before marking a host healthy, etc.):

* **HTTP**: During HTTP health checking Envoy will send an HTTP request to the upstream host. It
  expects a 200 response if the host is healthy. The upstream host can return 503 if it wants to
  immediately notify downstream hosts to no longer forward traffic to it.
* **L3/L4**: During L3/L4 health checking, Envoy will send a configurable byte buffer to the
  upstream host. It expects the byte buffer to be echoed in the response if the host is to be
  considered healthy.
* **Redis**: Envoy will send a Redis PING command and expect a PONG response. The upstream Redis
  server can respond with anything other than PONG to cause an immediate active health check
  failure.

Note that Envoy also supports passive health checking via :ref:`outlier detection
<arch_overview_outlier_detection>`.

.. _arch_overview_health_checking_filter:

HTTP health checking filter
---------------------------

When an Envoy mesh is deployed with active health checking between clusters, a large amount of
health checking traffic can be generated. Envoy includes an HTTP health checking filter that can be
installed in a configured HTTP listener. This filter is capable of a few different modes of
operation:

* **No pass through**: In this mode, the health check request is never passed to the local service.
  Envoy will respond with a 200 or a 503 depending on the current draining state of the server.
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

Health check filter :ref:`configuration <config_http_filters_health_check>`.

.. _arch_overview_health_checking_identity:

Health check identity
---------------------

Just verifying that an upstream host responds to a particular health check URL does not necessarily
mean that the upstream host is valid. For example, when using eventually consistent service
discovery in a cloud auto scaling or container environment, it's possible for a host to go away and
then come back with the same IP address, but as a different host type. One solution to this problem
is having a different HTTP health checking URL for every service type. The downside of that approach
is that overall configuration becomes more complicated as every health check URL is fully custom.

The Envoy HTTP health checker supports the :ref:`service_name
<config_cluster_manager_cluster_hc_service_name>` option. If this option is set, the health checker
additionally compares the value of the *x-envoy-upstream-healthchecked-cluster* response header to
*service_name*. If the values do not match, the health check does not pass. The upstream health
check filter appends *x-envoy-upstream-healthchecked-cluster* to the response headers. The appended
value is determined by the :option:`--service-cluster` command line option.
