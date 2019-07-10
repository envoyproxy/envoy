.. _config_http_filters_dynamic_forward_proxy:

Dynamic forward proxy
=====================

.. attention::

  HTTP dynamic forward proxy support should be considered alpha and not production ready.

* HTTP dynamic forward proxy :ref:`architecture overview <arch_overview_http_dynamic_forward_proxy>`
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.dynamic_forward_proxy.v2alpha.FilterConfig>`
* This filter should be configured with the name *envoy.filters.http.dynamic_forward_proxy*

The following is a complete configuration that configures both the
:ref:`dynamic forward proxy HTTP filter
<envoy_api_msg_config.filter.http.dynamic_forward_proxy.v2alpha.FilterConfig>`
as well as the :ref:`dynamic forward proxy cluster
<envoy_api_msg_config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig>`. Both filter and cluster
must be configured together and point to the same DNS cache parameters for Envoy to operate as an
HTTP dynamic forward proxy.

.. note::

  The HTTP connection manager :ref:`allow_absolute_url
  <envoy_api_field_core.Http1ProtocolOptions.allow_absolute_url>` parameter has been set to true
  to allow Envoy to proxy absolute HTTP URLs.

.. attention::

  While configuring a :ref:`tls_context <envoy_api_field_Cluster.tls_Context>` on the cluster with
  *trusted_ca* certificates instructs Envoy to use TLS when connecting to upstream hosts and verify
  the certificate chain, currently it is not possible to configure per-host TLS configuration
  parameters including SNI, subject alt name verification, etc. This will be added in a future
  change. **This means that the following configuration will not fully validate TLS certificates**.
  Use with care until full support for per-host validation is implemented.

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address:
        protocol: TCP
        address: 127.0.0.1
        port_value: 9901
  static_resources:
    listeners:
    - name: listener_0
      address:
        socket_address:
          protocol: TCP
          address: 0.0.0.0
          port_value: 10000
      filter_chains:
      - filters:
        - name: envoy.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.config.filter.network.http_connection_manager.v2.HttpConnectionManager
            stat_prefix: ingress_http
            http_protocol_options:
              allow_absolute_url: true
            route_config:
              name: local_route
              virtual_hosts:
              - name: local_service
                domains: ["*"]
                routes:
                - match:
                    prefix: "/"
                  route:
                    cluster: dynamic_forward_proxy_cluster
            http_filters:
            - name: envoy.filters.http.dynamic_forward_proxy
              config:
                dns_cache_config:
                  name: dynamic_forward_proxy_cache_config
                  dns_lookup_family: V4_ONLY
            - name: envoy.router
    clusters:
    - name: dynamic_forward_proxy_cluster
      connect_timeout: 1s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.dynamic_forward_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
          dns_cache_config:
            name: dynamic_forward_proxy_cache_config
            dns_lookup_family: V4_ONLY
      tls_context:
        common_tls_context:
          validation_context:
            trusted_ca: {filename: /etc/ssl/certs/ca-certificates.crt}

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
  host_address_changed, Counter, Number of DNS queries that resulted in a host address change.
  host_added, Counter, Number of hosts that have been added to the cache.
  host_removed, Counter, Number of hosts that have been removed from the cache.
  num_hosts, Gauge, Number of hosts that are currently in the cache.
