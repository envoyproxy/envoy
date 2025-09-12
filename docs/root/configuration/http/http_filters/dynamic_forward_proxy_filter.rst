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
  an untrusted network endpoint might establish DNS records that point to any of the aforementioned
  locations. Dynamic forward proxy servers should be protected by network firewalls, default-deny RBAC and
  other restrictions on container or kernel networking; the details are setup specific. Please
  consider carefully auditing the dynamic forward proxy server's networking configuration with the
  understanding that any address reachable from the proxy is potentially accessible by untrusted
  clients.

.. warning::

  If a custom preceding filter sets the ``envoy.upstream.dynamic_host`` and ``envoy.upstream.dynamic_port`` filter
  state, the HTTP Dynamic Forward Proxy filter might not function correctly unless the filter state values match the
  host used for DNS lookups by the filter. The Dynamic Forward Proxy cluster prioritizes cache lookups using the filter
  state values first, so mismatched hosts between the filter's resolution logic and the cluster's cache lookup can
  result in request failures.

.. note::

  Configuring a :ref:`transport_socket with name envoy.transport_sockets.tls <envoy_v3_api_field_config.cluster.v3.Cluster.transport_socket>` on the cluster with
  *trusted_ca* certificates instructs Envoy to use TLS when connecting to upstream hosts and verify
  the certificate chain. Additionally, Envoy will automatically perform SAN verification for the
  resolved host name as well as specify the host name via SNI.

.. _dns_cache_circuit_breakers:

Circuit Breakers
----------------

Dynamic Forward Proxy cluster has two types of circuit breakers:

1. **DNS Cache Circuit Breakers**: These are specific to the DNS resolution process. They limit the number of
   pending DNS requests and prevent overwhelming the resolver. These circuit breakers are configured through
   the :ref:`dns_cache_circuit_breaker <envoy_v3_api_field_extensions.common.dynamic_forward_proxy.v3.DnsCacheConfig.dns_cache_circuit_breaker>`
   field in the DNS cache configuration.

2. **Cluster Circuit Breakers**: In addition to the DNS-specific circuit breakers, the standard
   :ref:`cluster circuit breakers <config_cluster_manager_cluster_circuit_breakers>` also apply to the Dynamic
   Forward Proxy cluster. These limit connections, requests, retries, etc. to the upstream hosts and are configured
   like any other Envoy cluster.

.. literalinclude:: _include/dns-cache-circuit-breaker.yaml
    :language: yaml

The above example uses typed config :ref:`CaresDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig>`.
To use :ref:`AppleDnsResolverConfig<envoy_v3_api_msg_extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig>` (iOS/macOS only), follow the below example:

.. literalinclude:: _include/dns-cache-circuit-breaker-apple.yaml
    :language: yaml

Dynamic Host Resolution via Filter State
----------------------------------------

The dynamic forward proxy filter supports dynamic host and port resolution through filter state when the
:ref:`allow_dynamic_host_from_filter_state <envoy_v3_api_field_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig.allow_dynamic_host_from_filter_state>`
option is enabled. When this feature is enabled, the filter will check for the following filter state values
before falling back to the HTTP Host header:

* ``envoy.upstream.dynamic_host``: Specifies the target host for DNS resolution and connection.
* ``envoy.upstream.dynamic_port``: Specifies the target port for connection.

When the ``allow_dynamic_host_from_filter_state`` flag is enabled, the HTTP Dynamic Forward Proxy
filter will check for filter state values, providing consistency with the SNI and UDP Dynamic Forward
Proxy filters and allowing the same filter state mechanism to work across all proxy types.

Host Resolution Priority
^^^^^^^^^^^^^^^^^^^^^^^^

The filter resolves the target host and port using the following priority order:

When ``allow_dynamic_host_from_filter_state`` is enabled:

1. **Filter State Values:** ``envoy.upstream.dynamic_host`` and ``envoy.upstream.dynamic_port`` from the stream's filter state.
2. **Host Rewrite Configuration:** Values specified in the route's or virtual host's ``typed_per_filter_config``.
3. **HTTP Host Header:** The host and port from the incoming HTTP request's Host/Authority header.

When ``allow_dynamic_host_from_filter_state`` is disabled (default):

1. **Host Rewrite Configuration:** Values specified in the route's or virtual host's ``typed_per_filter_config``.
2. **HTTP Host Header:** The host and port from the incoming HTTP request's Host/Authority header.

Filter State Usage Example
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The filter state values can be set by other filters in the filter chain before the dynamic forward proxy
filter processes the request. For example, using a Set Filter State HTTP filter:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.set_filter_state
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
      on_request_headers:
      - object_key: "envoy.upstream.dynamic_host"
        format_string:
          text_format_source:
            inline_string: "example.com"
      - object_key: "envoy.upstream.dynamic_port"
        format_string:
          text_format_source:
            inline_string: "443"

Statistics
----------

The dynamic forward proxy DNS cache outputs statistics in the ``dns_cache.<dns_cache_name>.``
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
  dns_rq_pending_overflow, Counter, Number of DNS pending request overflow.

The dynamic forward proxy DNS cache circuit breakers output statistics in the ``dns_cache.<dns_cache_name>.circuit_breakers``
namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_pending_open, Gauge, Whether the requests circuit breaker is closed (0) or open (1).
  rq_pending_remaining, Gauge, Number of remaining requests until the circuit breaker opens.
