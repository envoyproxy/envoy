.. _arch_overview_http_dynamic_forward_proxy:

HTTP dynamic forward proxy
==========================

.. attention::

  HTTP dynamic forward proxy support should be considered alpha and not production ready. Stats
  as well as circuit breakers are missing and will be added soon.

Though the combination of both an :ref:`HTTP filter <config_http_filters_dynamic_forward_proxy>` and
:ref:`custom cluster <envoy_api_msg_config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig>`,
Envoy supports HTTP dynamic forward proxy. This means that Envoy can perform the role of an HTTP
proxy without prior knowledge of all configured DNS addresses, while still retaining the vast
majority of Envoy's benefits including asynchronous DNS resolution. The implementation works as
follows:

* The dynamic forward proxy HTTP filter is used to pause requests if the target DNS host is not
  already in cache.
* Envoy will begin asynchronously resolving the DNS address, unblocking any requests waiting on
  the response.
* Any future requests will not be blocked as the DNS address is already in cache. The resolution
  process works similarly to the :ref:`logical DNS
  <arch_overview_service_discovery_types_logical_dns>` service discovery type with a single target
  address being remembered at any given time.
* All known hosts are stored in the dynamic forward proxy cluster such that they can be displayed
  in :ref:`admin output <operations_admin_interface>`.
* A special load balancer will select the right host to use based on the HTTP host/authority header
  during forwarding.
* Hosts that have not been used for a period of time are subject to a TTL that will purge them.

The above implementation details mean that at steady state Envoy can forward a large volume of
HTTP proxy traffic while all DNS resolution happens asynchronously in the background. Additionally,
all other Envoy filters and extensions can be used in conjunction with dynamic forward proxy support
including authentication, RBAC, rate limiting, etc.

For further configuration information see the :ref:`HTTP filter configuration documentation
<config_http_filters_dynamic_forward_proxy>`.
