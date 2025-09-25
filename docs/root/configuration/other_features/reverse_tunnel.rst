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

1. **Downstream socket interface**: Registered as a bootstrap extension on initiator envoy to initiate and maintain reverse tunnels.
2. **Upstream socket interface**: Registered as a bootstrap extension on responder envoy to accept and manage reverse tunnels.
3. **Reverse tunnel network filter**: On responder Envoy to accept reverse tunnel requests. 
4. **Reverse connection cluster**: Added on responder Envoy for each downstream envoy node that needs to be reached through reverse tunnels.

.. _config_reverse_tunnel_configuration_files:

Configuration Files
-------------------

For practical examples and working configurations, see:

* :repo:`Initiator Envoy configuration <configs/reverse_connection/initiator-envoy.yaml>`: Configuration for the initiator Envoy (downstream)
* :repo:`Responder Envoy configuration <configs/reverse_connection/responder-envoy.yaml>`: Configuration for the responder Envoy (upstream)

.. _config_reverse_tunnel_initiator:

Initiator Configuration (Downstream Envoy)
-------------------------------------------

The initiator Envoy (downstream) requires the following configuration components to establish reverse tunnels:

Downstream Socket Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
      stat_prefix: "downstream_reverse_connection"

This extension enables the initiator to initiate and manage reverse tunnels to the responder Envoy.

Reverse Tunnel Listener
~~~~~~~~~~~~~~~~~~~~~~~~

The reverse tunnel listener triggers the reverse connection initiation to the upstream Envoy instance and encodes identity metadata for the local Envoy. It also contains the route configuration for downstream services reachable via reverse tunnels.

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: reverse_conn_listener
  address:
    socket_address:
      # Format: rc://src_node_id:src_cluster_id:src_tenant_id@remote_cluster:connection_count
      address: "rc://downstream-node:downstream-cluster:downstream-tenant@upstream-cluster:1"
      port_value: 0
      # Use custom resolver that can parse reverse connection metadata
      resolver_name: "envoy.resolvers.reverse_connection"
  filter_chains:
  - filters:
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
        stat_prefix: reverse_conn_listener
        route_config:
          virtual_hosts:
          - name: backend
            domains:
            - "*"
            routes:
            - match:
                prefix: '/downstream_service'
              route:
                cluster: downstream-service
        http_filters:
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

The special ``rc://`` address format encodes:

* ``src_node_id``: "downstream-node" - Unique identifier for this downstream node
* ``src_cluster_id``: "downstream-cluster" - Cluster name of the downstream Envoy
* ``src_tenant_id``: "downstream-tenant" - Tenant identifier
* ``remote_cluster``: "upstream-cluster" - Name of the upstream cluster to connect to
* ``connection_count``: "1" - Number of reverse connections to establish

The 'downstream-service' cluster is the service behind initiator envoy that will be accessed via reverse tunnels from behind the responder envoy.

.. validated-code-block:: yaml
  :type-name: envoy.config.cluster.v3.Cluster

  name: downstream-service
  type: STRICT_DNS
  connect_timeout: 30s
  load_assignment:
    cluster_name: downstream-service
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: downstream-service
              port_value: 80

Upstream Cluster
~~~~~~~~~~~~~~~~~

Each upstream envoy to which reverse tunnels should be established needs to be configured with a cluster, added via CDS.

.. validated-code-block:: yaml
  :type-name: envoy.config.cluster.v3.Cluster

  name: upstream-cluster
  type: STRICT_DNS
  connect_timeout: 30s
  load_assignment:
    cluster_name: upstream-cluster
    endpoints:
    - lb_endpoints:
      - endpoint:
          address:
            socket_address:
              address: upstream-envoy  # Responder Envoy address
              port_value: 9000         # Port where responder listens for reverse tunnel requests

Multiple Cluster Support
~~~~~~~~~~~~~~~~~~~~~~~~~

To initiate reverse tunnels to multiple upstream clusters, each such cluster needs to be configured under an additional address section.

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

TLS Configuration (Optional)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For secure reverse tunnel establishment, add a TLS context to the upstream cluster:

.. validated-code-block:: yaml
  :type-name: envoy.config.cluster.v3.Cluster

  name: upstream-cluster
  type: STRICT_DNS
  connect_timeout: 30s
  transport_socket:
    name: envoy.transport_sockets.tls
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
      common_tls_context:
        tls_certificates:
        - certificate_chain:
            filename: "/etc/ssl/certs/client-cert.pem"
          private_key:
            filename: "/etc/ssl/private/client-key.pem"
        validation_context:
          filename: "/etc/ssl/certs/ca-cert.pem"
          verify_certificate_spki:
          - "NdQcW/8B5PcygH/5tnDNXeA2WS/2JzV3K1PKz7xQlKo="
        alpn_protocols: ["h2", "http/1.1"]
      sni: upstream-envoy.example.com

This configuration enables mTLS authentication between the downstream and upstream Envoys.

.. _config_reverse_tunnel_responder:

Responder Configuration (Upstream Envoy)
-----------------------------------------

The responder Envoy (upstream) requires the following configuration components to accept reverse tunnels:

Bootstrap Extension for Socket Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
      stat_prefix: "upstream_reverse_connection"

This extension enables the responder to accept and manage reverse connections from initiator Envoys.

Reverse Tunnel Network Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The reverse tunnel network filter implements the reverse tunnel handshake protocol and accepts or rejects reverse tunnel requests:

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: rev_conn_api_listener
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 9000  # Port where initiator will connect for tunnel establishment
  filter_chains:
  - filters:
    - name: envoy.filters.network.reverse_tunnel
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel
        ping_interval: 2s

The ``envoy.filters.network.reverse_tunnel`` network filter handles the reverse tunnel handshake protocol and connection acceptance.

.. _config_reverse_connection_handshake:

Handshake Protocol
~~~~~~~~~~~~~~~~~~

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
~~~~~~~~~~~~~~~~~~~~~~~~~~

Each downstream node reachable from upstream Envoy via reverse connections needs to be configured with a reverse connection cluster. When a data request arrives at the upstream Envoy, this cluster uses cached "reverse connections" instead of creating new forward connections.

.. code-block:: yaml
  :type-name: envoy.config.cluster.v3.Cluster

  name: reverse_connection_cluster
  connect_timeout: 200s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.reverse_connection
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
      http_header_names:
      - x-remote-node-id     # Should be set to "downstream-node"
      - x-dst-cluster-uuid   # Should be set to "downstream-cluster"
  typed_extension_protocol_options:
    envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
      "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
      explicit_http_config:
        http2_protocol_options: {}  # HTTP/2 required for reverse connections

The reverse connection cluster configuration specifies:

* **Load balancing policy**: ``CLUSTER_PROVIDED`` allows the custom cluster to manage load balancing
* **Header Resolution Strategy**: The cluster follows a tiered approach to identify the target downstream node:

  1. **Configured Headers**: First checks for headers specified in ``http_header_names`` configuration. If not configured, defaults to ``x-envoy-dst-node-uuid`` and ``x-envoy-dst-cluster-uuid``
  2. **Host Header**: If no configured headers are found, extracts UUID from the Host header in format ``<uuid>.tcpproxy.envoy.remote:<port>``
  3. **SNI (Server Name Indication)**: If Host header extraction fails, extracts UUID from SNI in format ``<uuid>.tcpproxy.envoy.remote``
* **Protocol**: Only HTTP/2 is supported for reverse connections
* **Host Reuse**: Once a host is created for a specific downstream node ID, it is cached and reused for all subsequent requests to that node. Each such request is multiplexed as a new stream on the existing HTTP/2 connection.


Egress Listener for Data Traffic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add an egress listener on upstream envoy that accepts data traffic and routes it to the reverse connection cluster.

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  name: egress_listener
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 8085  # Port for sending requests to initiator services
  filter_chains:
  - filters:
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
        route_config:
          virtual_hosts:
          - name: backend
            domains: ["*"]
            routes:
            - match:
                prefix: "/downstream_service"
              route:
                cluster: reverse_connection_cluster  # Routes to initiator via reverse tunnel

.. _config_reverse_connection_stats:

Statistics
----------

The reverse tunnel extensions emit the following statistics:

**Reverse Tunnel Filter:**

The reverse tunnel network filter emits handshake-related statistics with the prefix ``reverse_tunnel.handshake.``:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   reverse_tunnel.handshake.parse_error, Counter, Number of handshake requests with missing required headers
   reverse_tunnel.handshake.accepted, Counter, Number of successfully accepted reverse tunnel connections
   reverse_tunnel.handshake.rejected, Counter, Number of rejected reverse tunnel connections

**Downstream Socket Interface:**

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

**Upstream Socket Interface:**

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

* **Authentication**: Implement proper authentication mechanisms for handshake validation as part of the reverse tunnel handshake protocol.
* **Authorization**: Validate that downstream nodes are authorized to connect to upstream clusters.
* **TLS**: TLS can be configured for each upstream cluster reverse tunnels are established to.

