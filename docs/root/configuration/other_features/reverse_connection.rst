.. _config_reverse_connection:

Reverse Connection
==================

Envoy supports reverse connections that enable establishing persistent connections from downstream Envoy instances
to upstream Envoy instances without requiring the upstream to be directly reachable from the downstream.
This feature is particularly useful in scenarios where downstream instances are behind NATs, firewalls,
or in private networks, and need to initiate connections to upstream instances in public networks or cloud environments.

Reverse connections work by having the downstream Envoy initiate connections to upstream Envoy instances
and keep them alive for reuse. These connections are established using a handshake protocol and can be
used for forwarding downstream traffic to upstream services.

.. _config_reverse_connection_bootstrap:

Bootstrap Configuration
-----------------------

To enable reverse connections, two bootstrap extensions need to be configured:

1. **Downstream Reverse Connection Socket Interface**: Configures the downstream Envoy to initiate
   reverse connections to upstream instances.

2. **Upstream Reverse Connection Socket Interface**: Configures the upstream Envoy to accept
   and manage reverse connections from downstream instances.

.. _config_reverse_connection_downstream:

Downstream Configuration
~~~~~~~~~~~~~~~~~~~~~~~~

The downstream reverse connection socket interface is configured in the bootstrap as follows:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
      stat_prefix: "downstream_reverse_connection"

.. _config_reverse_connection_upstream:

Upstream Configuration
~~~~~~~~~~~~~~~~~~~~~~

The upstream reverse connection socket interface is configured in the bootstrap as follows:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
      stat_prefix: "upstream_reverse_connection"

.. _config_reverse_connection_listener:

Listener Configuration
----------------------

Reverse connections are initiated through special listeners that use the reverse connection address format:

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: reverse_connection_listener
  address:
    socket_address:
      address: "rc://downstream-node-id:downstream-cluster-id:downstream-tenant-id@upstream-cluster:connection-count"
      port_value: 0
  filter_chains:
  - filters:
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: upstream-cluster

The reverse connection address format ``rc://src_node:src_cluster:src_tenant@target_cluster:count``
encodes the following information:

* ``src_node``: Unique identifier for the downstream node
* ``src_cluster``: Cluster name of the downstream Envoy
* ``src_tenant``: Tenant identifier for multi-tenant deployments
* ``target_cluster``: Name of the upstream cluster to connect to
* ``count``: Number of reverse connections to establish

.. _config_reverse_connection_handshake:

Handshake Protocol
------------------

Reverse connections use a handshake protocol to establish authenticated connections between
downstream and upstream Envoy instances. The handshake includes:

1. **Connection Initiation**: Downstream Envoy connects to upstream using configured cluster information
2. **Identity Exchange**: Nodes exchange identity information (node ID, cluster ID, tenant ID)
3. **Authentication**: Optional authentication and authorization checks
4. **Connection Establishment**: Successful handshake allows connection reuse for data traffic

The handshake protocol uses HTTP as the transport mechanism with protobuf-encoded messages
for reliable message exchange and parsing.

.. _config_reverse_connection_stats:

Statistics
----------

The reverse connection extensions emit the following statistics:

**Downstream Extension:**

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   connections_total, Counter, Total number of reverse connections initiated
   connections_active, Gauge, Number of currently active reverse connections
   connections_successful, Counter, Number of successfully established connections
   connections_failed, Counter, Number of failed connection attempts
   handshake_successful, Counter, Number of successful handshakes
   handshake_failed, Counter, Number of failed handshakes

**Upstream Extension:**

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   connections_accepted, Counter, Number of reverse connections accepted
   connections_active, Gauge, Number of currently active accepted connections
   connections_rejected, Counter, Number of connections rejected during handshake
   sockets_cached, Gauge, Number of cached sockets available for reuse
   sockets_reused, Counter, Number of times cached sockets were reused

.. _config_reverse_connection_security:

Security Considerations
-----------------------

Reverse connections should be used with appropriate security measures:

* **Authentication**: Implement proper authentication mechanisms for handshake validation
* **Authorization**: Validate that downstream nodes are authorized to connect to upstream clusters
* **TLS**: Use TLS transport sockets for encrypted communication
* **Network Policies**: Restrict network access to only allow expected downstream-to-upstream communication
* **Monitoring**: Monitor connection statistics and handshake failures for security anomalies

.. _config_reverse_connection_examples:

Examples
--------

.. _config_reverse_connection_simple:

Simple Reverse Connection
~~~~~~~~~~~~~~~~~~~~~~~~~

A basic example connecting a downstream Envoy to an upstream cluster:

**Downstream Configuration:**

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
      stat_prefix: "downstream_rc"

  static_resources:
    listeners:
    - name: reverse_listener
      address:
        socket_address:
          address: "rc://node-1:downstream-cluster:tenant-a@upstream-cluster:3"
          port_value: 0
      filter_chains:
      - filters:
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcp
            cluster: upstream-cluster

    clusters:
    - name: upstream-cluster
      type: LOGICAL_DNS
      dns_lookup_family: V4_ONLY
      load_assignment:
        cluster_name: upstream-cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: "upstream.example.com"
                  port_value: 8080

**Upstream Configuration:**

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
      stat_prefix: "upstream_rc"

  static_resources:
    listeners:
    - name: upstream_listener
      address:
        socket_address:
          address: "0.0.0.0"
          port_value: 8080
      filter_chains:
      - filters:
        - name: envoy.filters.network.tcp_proxy
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
            stat_prefix: tcp
            cluster: backend

    clusters:
    - name: backend
      type: LOGICAL_DNS
      dns_lookup_family: V4_ONLY
      load_assignment:
        cluster_name: backend
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: "backend.example.com"
                  port_value: 9000

.. _config_reverse_connection_multi_cluster:

Multiple Clusters
~~~~~~~~~~~~~~~~~

Configure reverse connections to multiple upstream clusters:

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: multi_cluster_listener
  address:
    socket_address:
      address: "rc://node-1:downstream-cluster:tenant-a@cluster-a:2,cluster-b:3"
      port_value: 0
  filter_chains:
  - filters:
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: dynamic_cluster

This configuration establishes:
* 2 connections to ``cluster-a``
* 3 connections to ``cluster-b``

.. _config_reverse_connection_tls:

TLS-Enabled Reverse Connections
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add TLS encryption to reverse connections:

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: tls_reverse_listener
  address:
    socket_address:
      address: "rc://node-1:downstream-cluster:tenant-a@upstream-cluster:2"
      port_value: 0
  filter_chains:
  - transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "/etc/ssl/certs/downstream.crt"
            private_key:
              filename: "/etc/ssl/private/downstream.key"
          validation_context:
            trusted_ca:
              filename: "/etc/ssl/certs/ca.crt"
    filters:
    - name: envoy.filters.network.tcp_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
        stat_prefix: tcp
        cluster: upstream-cluster
