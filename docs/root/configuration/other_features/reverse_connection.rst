.. _config_reverse_connection:

Reverse Tunnels
===============

Envoy supports reverse tunnels that enable establishing persistent connections from downstream Envoy instances
to upstream Envoy instances without requiring the upstream to be directly reachable from the downstream.
This feature is particularly useful in scenarios where downstream instances are behind NATs, firewalls,
or in private networks, and need to initiate connections to upstream instances in public networks or cloud environments.

Reverse tunnels work by having the downstream Envoy initiate TCP connections to upstream Envoy instances
and keep them alive for reuse. These connections are established using a handshake protocol and can be
used for forwarding traffic from services behind upstream Envoy to downstream services behind the downstream Envoy.

.. _config_reverse_tunnel_bootstrap:

Reverse tunnels require the following extensions:

1. **Downstream and upstream socket interfaces**: Both registered as bootstrap extensions
2. **Reverse tunnel network filter**: On responder Envoy to accept reverse tunnel requests  
3. **Reverse connection cluster**: On responder Envoy for each downstream cluster that needs to be reached through reverse tunnels

Bootstrap Extensions
--------------------

To enable reverse tunnels, two bootstrap extensions need to be configured:

1. **Downstream Socket Interface**: Configures the downstream Envoy to initiate
   reverse tunnels to upstream instances.

2. **Upstream Socket Interface**: Configures the upstream Envoy to accept
   and manage reverse tunnels from downstream instances.

.. _config_reverse_connection_downstream:

Downstream Socket Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The downstream socket interface is configured in the bootstrap as follows:

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
      stat_prefix: "downstream_reverse_connection"

.. _config_reverse_connection_upstream:

Upstream Socket Interface
~~~~~~~~~~~~~~~~~~~~~~

The upstream socket interface is configured in the bootstrap as follows:

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

Reverse tunnels are initiated through special reverse tunnel listeners that use the following
address format:

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

The reverse tunnel address format ``rc://src_node:src_cluster:src_tenant@target_cluster:count``
encodes the following information:

* ``src_node``: Unique identifier for the downstream node
* ``src_cluster``: Cluster name of the downstream Envoy
* ``src_tenant``: Tenant identifier for multi-tenant deployments
* ``target_cluster``: Name of the upstream cluster to connect to
* ``count``: Number of reverse tunnels to establish to upstream-cluster

The upstream-cluster can be dynamically configurable via CDS. The listener calls the reverse tunnel
workflow and initiates raw TCP connections to upstream clusters, thereby This triggering the reverse
connection handshake.

.. _config_reverse_tunnel_filter:

Reverse Tunnel Network Filter
-----------------------------

On upstream Envoy, the reverse tunnel network filter implements the reverse tunnel handshake protocol and accepts or rejects the reverse tunnel request.

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: rev_conn_api_listener
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 9000
  filter_chains:
  - filters:
    - name: envoy.filters.network.reverse_tunnel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
        ping_interval: 2s

.. _config_reverse_connection_handshake:

Handshake Protocol
------------------

Reverse tunnels use a handshake protocol to establish authenticated connections between
downstream and upstream Envoy instances. The handshake has the following steps:

1. **Connection Initiation**: Initiator Envoy initiates TCP connections to each host of the upstream cluster,
and writes the handshake request on it over HTTP.
2. **Identity Exchange**: The downstream Envoy's reverse tunnel handshake contains identity information (node ID, cluster ID, tenant ID) sent as HTTP headers. The reverse tunnel network filter expects the following headers:

   * ``x-envoy-reverse-tunnel-node-id``: Unique identifier for the downstream node (e.g., "on-prem-node")
   * ``x-envoy-reverse-tunnel-cluster-id``: Cluster name of the downstream Envoy (e.g., "on-prem")
   * ``x-envoy-reverse-tunnel-tenant-id``: Tenant identifier for multi-tenant deployments (e.g., "on-prem")

   These identify values are obtained from the reverse tunnel listener address and the headers are automatically added by the reverse tunnel downstream socket interface during the handshake process.
3. **Validation/Authentication**: The upstream Envoy performs the following validation checks on receiving the handshake request:

   * **HTTP Method Validation**: Verifies the request method matches the configured method (defaults to ``GET``)
   * **HTTP Path Validation**: Verifies the request path matches the configured path (defaults to ``/reverse_connections/request``)
   * **Required Headers Validation**: Ensures all three required identity headers are present:
     
     - ``x-envoy-reverse-tunnel-node-id``
     - ``x-envoy-reverse-tunnel-cluster-id``
     - ``x-envoy-reverse-tunnel-tenant-id``

   If any validation fails, the request is rejected with appropriate HTTP error codes (404 for method/path mismatch, 400 for missing headers).
4. **Connection Establishment**: Post a successful handshake, the upstream Envoy stores the TCP socket mapped to the downstream node ID.

.. _config_reverse_connection_cluster:

Reverse Connection Cluster
--------------------------

Each initiator node that should be reachable via reverse tunnels must be configured using a reverse connection cluster. This is a custom cluster type that indicates that instead of creating new forward connections to the downstream node, cached "reverse connections" should be used to send requests.

The reverse connection cluster uses the ``envoy.clusters.reverse_connection`` cluster type and requires specific HTTP headers in downstream requests to identify which cached reverse connection to use for routing.

.. .. validated-code-block:: yaml
..   :type-name: envoy.config.cluster.v3.Cluster

..   name: reverse_connection_cluster
..   connect_timeout: 200s
..   lb_policy: CLUSTER_PROVIDED
..   cluster_type:
..     name: envoy.clusters.reverse_connection
..     typed_config:
..       "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
..       # The following headers are expected in downstream requests
..       # to be sent over reverse connections
..       http_header_names:
..         - x-remote-node-id # Should be set to the node ID of the downstream envoy node, ie., on-prem-node
..         - x-dst-cluster-uuid # Should be set to the cluster ID of the downstream envoy node, ie., on-prem

The cluster configuration specifies:

* **Load balancing policy**: ``CLUSTER_PROVIDED`` allows the custom cluster to manage load balancing
* **Header Resolution Strategy**: The cluster follows a tiered approach to identify the target downstream node:

  1. **Configured Headers**: First checks for headers specified in ``http_header_names`` configuration. If not configured, defaults to ``x-envoy-dst-node-uuid`` and ``x-envoy-dst-cluster-uuid``
  2. **Host Header**: If no configured headers are found, extracts UUID from the Host header in format ``<uuid>.tcpproxy.envoy.remote:<port>``
  3. **SNI (Server Name Indication)**: If Host header extraction fails, extracts UUID from SNI in format ``<uuid>.tcpproxy.envoy.remote``
* **Protocol**: Only HTTP/2 is supported for reverse connections
* **Host Reuse**: Once a host is created for a specific downstream node ID, it is cached and reused for all subsequent requests to that node. Each such request is multiplexed as a new stream on the existing HTTP/2 connection.

.. _config_reverse_connection_stats:

Statistics
----------

The reverse tunnel extensions emit the following statistics:

**Downstream Extension:**

The downstream reverse tunnel extension emits both host-level and cluster-level statistics for connection states. The stat names follow the pattern:

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

The upstream reverse tunnel extension emits node-level and cluster-level statistics for accepted connections. The stat names follow the pattern:

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

Reverse tunnels should be used with appropriate security measures:

* **Authentication**: Implement proper authentication mechanisms for handshake validation as part of the reverse tunnel handshake protocol
* **Authorization**: Validate that downstream nodes are authorized to connect to upstream clusters
* **TLS**: TLS can be configured for each upstream cluster reverse tunnels are established to

