.. _config_http_filters_dynamic_forward_proxy:

Dynamic forward proxy
=====================

* HTTP dynamic forward proxy :ref:`architecture overview <arch_overview_http_dynamic_forward_proxy>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig>`

The following is a complete configuration that configures both the
:ref:`dynamic forward proxy HTTP filter
<envoy_v3_api_msg_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig>`
as well as the :ref:`dynamic forward proxy cluster
<envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`. Both filter and cluster
must be configured together and point to the same DNS cache parameters for Envoy to operate as an
HTTP dynamic forward proxy.

This filter supports :ref:`host rewrite <envoy_v3_api_msg_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig>`
via the :ref:`virtual host's typed_per_filter_config <envoy_v3_api_field_config.route.v3.VirtualHost.typed_per_filter_config>` or the
:ref:`route's typed_per_filter_config <envoy_v3_api_field_config.route.v3.Route.typed_per_filter_config>`. This can be used to rewrite
the host header with the provided value before DNS lookup, thus allowing to route traffic to the rewritten
host when forwarding. See the example below within the configured routes.

.. warning::

  Servers operating dynamic forward proxy in environments where either client or destination are
  untrusted are subject to confused deputy attacks. For example, a client may attempt to use the
  dynamic forward capability to access a port on the server's localhost, link-local addresses,
  Cloud-provider metadata server or the private network in which the proxy is operating. Similarly,
  an untrusted network endpoint might establish DNS records that point to any of the forementioned
  locations. Dynamic forward proxy servers should be protected by network firewalls, default-deny RBAC and
  other restrictions on container or kernel networking; the details are setup specific. Please
  consider carefully auditing the dynamic forward proxy server's networking configuration with the
  understanding that any address reachable from the proxy is potentially accessible by untrusted
  clients.

.. note::

  Configuring a :ref:`transport_socket with name envoy.transport_sockets.tls <envoy_v3_api_field_config.cluster.v3.Cluster.transport_socket>` on the cluster with
  *trusted_ca* certificates instructs Envoy to use TLS when connecting to upstream hosts and verify
  the certificate chain. Additionally, Envoy will automatically perform SAN verification for the
  resolved host name as well as specify the host name via SNI.

.. _dns_cache_circuit_breakers:

  Dynamic forward proxy uses circuit breakers built in to the DNS cache with the configuration
  of :ref:`DNS cache circuit breakers <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_cache_circuit_breaker>`.

.. literalinclude:: _include/dns-cache-circuit-breaker.yaml
    :language: yaml

Above example is using typed config :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>`.
To use :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>` (iOS/macOS only), follow below example:

.. literalinclude:: _include/dns-cache-circuit-breaker-apple.yaml
    :language: yaml

Statistics
----------

The dynamic forward proxy DNS cache outputs statistics in the dns_cache.<dns_cache_name>.*
namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  dns_query_attempt, Counter, Number of DNS query attempts.
  dns_query_success, Counter, Number of DNS query successes.
  dns_query_failure, Counter, Number of DNS query failures.
  dns_query_timeout, Counter, Number of DNS query :ref:`timeouts <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_query_timeout>`.
  host_address_changed, Counter, Number of DNS queries that resulted in a host address change.
  host_added, Counter, Number of hosts that have been added to the cache.
  host_removed, Counter, Number of hosts that have been removed from the cache.
  num_hosts, Gauge, Number of hosts that are currently in the cache.
  dns_rq_pending_overflow, Counter, Number of dns pending request overflow.

The dynamic forward proxy DNS cache circuit breakers outputs statistics in the dns_cache.<dns_cache_name>.circuit_breakers*
namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_pending_open, Gauge, Whether the requests circuit breaker is closed (0) or open (1)
  rq_pending_remaining, Gauge, Number of remaining requests until the circuit breaker opens
