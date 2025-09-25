.. _config_reverse_connection:

Reverse Connection
==================

Envoy supports reverse connections that enable establishing persistent connections from downstream Envoy instances
to upstream Envoy instances without requiring the upstream to be directly reachable from the downstream.
This feature is particularly useful in scenarios where downstream instances are behind NATs, firewalls,
or in private networks, and need to initiate connections to upstream instances in public networks or cloud environments.

Reverse connections work by having the downstream Envoy initiate TCP connections to upstream Envoy instances
and keep them alive for reuse. These connections are established using a handshake protocol and can be
used for forwarding traffic from services behind upstream Envoy to downstream services behind the downstream Envoy.

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

Reverse connections are initiated through special reverse connection listeners that use the following
reverse connection address format:

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
* ``count``: Number of reverse connections to establish to upstream-cluster

The upstream-cluster can be dynamically configurable via CDS. The listener calls the reverse connection
workflow and initiates raw TCP connections to upstream clusters, thereby This triggering the reverse
connection handshake.

.. _config_reverse_connection_handshake:

Handshake Protocol
------------------

Reverse connections use a handshake protocol to establish authenticated connections between
downstream and upstream Envoy instances. The handshake has the following steps:

1. **Connection Initiation**: Downstream Envoy initiates TCP connections to each host of the upstream cluster,
and writes the handshake request on it over a HTTP/1.1 POST call.
2. **Identity Exchange**: The downstream Envoy's reverse connection handshake contains identity information (node ID, cluster ID, tenant ID).
3. **Authentication**: Optional authentication and authorization checks are performed by the upstream Envoy on receiving the handshake request.
4. **Connection Establishment**: Post a successful handshake, the upstream Envoy stores the TCP socket mapped to the downstream node ID.

.. _config_reverse_connection_stats:

Statistics
----------

The reverse connection extensions emit the following statistics:

**Downstream Extension:**

The downstream reverse connection extension emits both host-level and cluster-level statistics for connection states. The stat names follow the pattern:

- Host-level: ``<stat_prefix>.host.<host_address>.<state>``
- Cluster-level: ``<stat_prefix>.cluster.<cluster_id>.<state>``

Where ``<state>`` can be one of:

.. csv-table::
   :header: State, Type, Description
   :widths: 1, 1, 2

   connecting, Gauge, Number of connections currently being established
   connected, Gauge, Number of successfully established connections
   failed, Gauge, Number of failed connection attempts
   recovered, Gauge, Number of connections that recovered from failure
   backoff, Gauge, Number of hosts currently in backoff state
   cannot_connect, Gauge, Number of connection attempts that could not be initiated
   unknown, Gauge, Number of connections in unknown state (fallback)

For example, with ``stat_prefix: "downstream_rc"``:
- ``downstream_rc.host.192.168.1.1.connecting`` - connections being established to host 192.168.1.1
- ``downstream_rc.cluster.upstream-cluster.connected`` - established connections to upstream-cluster

**Upstream Extension:**

The upstream reverse connection extension emits node-level and cluster-level statistics for accepted connections. The stat names follow the pattern:

- Node-level: ``reverse_connections.nodes.<node_id>``
- Cluster-level: ``reverse_connections.clusters.<cluster_id>``

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   reverse_connections.nodes.<node_id>, Gauge, Number of active connections from downstream node
   reverse_connections.clusters.<cluster_id>, Gauge, Number of active connections from downstream cluster

For example:
- ``reverse_connections.nodes.node-1`` - active connections from downstream node "node-1"
- ``reverse_connections.clusters.downstream-cluster`` - active connections from downstream cluster "downstream-cluster"

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

A basic configuration example for using the downstream and upstream reverse connection socket interfaces
are shown below.

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
      address: "rc://node-1:downstream-cluster:tenant-a@cluster-a:2"
      port_value: 0
  additional_addresses:
  - address:
      socket_address:
        address: "rc://node-1:downstream-cluster:tenant-a@cluster-b:3"
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
