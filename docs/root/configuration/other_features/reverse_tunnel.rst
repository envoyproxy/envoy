.. _config_reverse_tunnel:

Reverse Tunnels
===============

.. attention::

  The reverse tunnels feature is experimental and is currently under active development.

Envoy supports reverse tunnels that enable establishing persistent connections from downstream Envoy
instances to upstream Envoy instances without requiring the upstream to be directly reachable from the
downstream. This feature is particularly useful in scenarios where downstream instances are behind NATs,
firewalls, or in private networks, and need to communicate with upstream instances in public networks
or cloud environments.

Reverse tunnels invert the typical connection model: the downstream Envoy initiates TCP connections to
upstream Envoy instances and keeps them alive for reuse. These connections are established using a
handshake protocol, after which traffic can be forwarded bidirectionally. Services behind the upstream
Envoy can send requests through the tunnel to downstream services behind the initiator Envoy, effectively
treating the normally unreachable downstream services as if they were directly accessible.

.. image:: images/reverse_tunnel_arch.png
   :width: 80%
   :align: center

.. _config_reverse_tunnel_bootstrap:

Reverse tunnels require the following extensions:

1. **Downstream socket interface**: Registered as a bootstrap extension on the initiator Envoy to initiate and maintain reverse tunnels.
2. **Upstream socket interface**: Registered as a bootstrap extension on the responder Envoy to accept and manage reverse tunnels.
3. **Reverse tunnel network filter**: Configured on the responder Envoy to accept and validate reverse tunnel handshake requests.
4. **Reverse connection cluster**: Configured on the responder Envoy to route data requests to downstream nodes through established reverse tunnels.

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

.. _config_reverse_tunnel_downstream_socket_interface:

Downstream Socket Interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": >-
        type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
      stat_prefix: "downstream_reverse_connection"

This extension enables the initiator Envoy to establish and maintain reverse tunnel connections to the responder Envoy.

.. _config_reverse_tunnel_listener:

Reverse Tunnel Listener
~~~~~~~~~~~~~~~~~~~~~~~~

The reverse tunnel listener triggers reverse connection initiation to the upstream Envoy and encodes
identity metadata for the local Envoy instance. The listener's address field uses a special ``rc://``
format to specify connection parameters, and its route configuration defines which downstream services
are reachable through the reverse tunnel.

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

The special ``rc://`` address format encodes connection and identity metadata:

``rc://src_node_id:src_cluster_id:src_tenant_id@remote_cluster:connection_count``

In the example above, this expands to:

* ``src_node_id``: ``downstream-node`` - Unique identifier for this specific Envoy instance.
* ``src_cluster_id``: ``downstream-cluster`` - Logical grouping identifier for this Envoy and its peers.
* ``src_tenant_id``: ``downstream-tenant`` - Tenant identifier for multi-tenant isolation.
* ``remote_cluster``: ``upstream-cluster`` - Name of the upstream cluster to connect to.
* ``connection_count``: ``1`` - Number of reverse connections to establish to the remote cluster.

The identifiers serve the following purposes:

* **src_node_id**: Each node must have a unique ``src_node_id`` across the entire system to ensure proper routing and connection management. Data requests can target a specific node by its ID.
* **src_cluster_id**: Multiple nodes can share the same ``src_cluster_id``, forming a logical group. Data requests sent using the cluster ID will be load balanced across all nodes in that cluster. The ``src_cluster_id`` must not collide with any ``src_node_id``.
* **src_tenant_id**: Used in multi-tenant environments to isolate traffic and resources between different tenants or organizational units.

The ``downstream-service`` cluster in the example refers to the service behind the initiator Envoy that will be accessed via reverse tunnels from services behind the responder Envoy.

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

Each upstream Envoy to which reverse tunnels should be established requires a cluster configuration.
This cluster can be defined statically in the bootstrap configuration or added dynamically via the
:ref:`Cluster Discovery Service (CDS) <config_cluster_manager_cds>`.

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

To establish reverse tunnels to multiple upstream clusters simultaneously, use the ``additional_addresses``
field on the listener. Each address in this list specifies an additional upstream cluster and the number
of connections to establish to it.

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

TLS Configuration
~~~~~~~~~~~~~~~~~

For secure reverse tunnel establishment, configure a TLS transport socket on the upstream cluster.
The example below shows mutual TLS (mTLS) configuration with certificate pinning:

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

This configuration provides mutual TLS authentication between the initiator and responder Envoys.
The client certificate authenticates the initiator, while the server certificate and SPKI pinning
authenticate the responder. The ALPN configuration negotiates HTTP/2, which is required for reverse
tunnel operation.

.. _config_reverse_tunnel_responder:

Responder Configuration (Upstream Envoy)
-----------------------------------------

The responder Envoy (upstream) requires the following configuration components to accept reverse tunnels:

.. _config_reverse_tunnel_upstream_socket_interface:

Upstream Socket Interface
~~~~~~~~~~~~~~~~~~~~~~~~~

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.Bootstrap

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
    typed_config:
      "@type": >-
        type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
      stat_prefix: "upstream_reverse_connection"

This extension enables the responder Envoy to accept and manage incoming reverse tunnel connections from initiator Envoys.

.. _config_reverse_tunnel_network_filter:

Reverse Tunnel Network Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``envoy.filters.network.reverse_tunnel`` network filter implements the reverse tunnel handshake
protocol. It validates incoming connection requests and accepts or rejects them based on the handshake
parameters. The filter also manages periodic ping/pong keep-alive messages to maintain tunnel health.

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

.. _config_reverse_connection_cluster:

Reverse Connection Cluster
~~~~~~~~~~~~~~~~~~~~~~~~~~

The reverse connection cluster is a special cluster type that routes traffic through established reverse
tunnels rather than creating new outbound connections. When a data request arrives at the upstream Envoy
for a downstream node, the cluster looks up a cached reverse tunnel connection to that node and reuses it.

Each data request must include a ``host_id`` that identifies the target downstream node. This ID can be
specified directly in request headers or computed from them. The cluster extracts the ``host_id`` using
the configured ``host_id_format`` field and uses it to look up the appropriate reverse tunnel connection.

.. validated-code-block:: yaml
  :type-name: envoy.config.cluster.v3.Cluster

  name: reverse_connection_cluster
  connect_timeout: 200s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.reverse_connection
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
      cleanup_interval: 60s
      host_id_format: "%REQ(x-computed-host-id)%"
  typed_extension_protocol_options:
    envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
      "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
      explicit_http_config:
        http2_protocol_options: {}  # HTTP/2 required for reverse connections

The reverse connection cluster configuration includes several key fields:

**Load balancing policy**
  Must be set to ``CLUSTER_PROVIDED`` to delegate load balancing to the custom cluster implementation.

**Host ID format**
  The ``host_id_format`` field uses Envoy's :ref:`formatter system <config_access_log_format_strings>` to
  extract the target downstream node identifier from the request context. Supported formatters include:

  * ``%REQ(header-name)%``: Extract value from a request header.
  * ``%DYNAMIC_METADATA(namespace:key)%``: Extract value from dynamic metadata.
  * ``%FILTER_STATE(key)%``: Extract value from filter state.
  * ``%DOWNSTREAM_REMOTE_ADDRESS%``: Use the downstream connection address.
  * Plain text and combinations of the above.

  See the :ref:`config_reverse_connection_egress_listener` section for an example of processing headers
  to set the ``host_id``.

**Protocol**
  Only HTTP/2 is supported for reverse connections. This is required to support multiplexing multiple
  data requests over a single TCP connection.

**Connection reuse**
  Once a connection is established to a specific downstream node, it is cached and reused for all subsequent
  requests to that node. Each data request is multiplexed as a new HTTP/2 stream on the existing connection,
  avoiding the overhead of establishing new connections.

.. _config_reverse_connection_egress_listener:

Egress Listener for Data Traffic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An egress listener on the upstream Envoy accepts data requests and routes them to the reverse connection
cluster. This listener typically includes header processing logic to extract or compute the ``host_id``
that identifies the target downstream node for each request.

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
        stat_prefix: egress_http
        route_config:
          virtual_hosts:
          - name: backend
            domains: ["*"]
            routes:
            - match:
                prefix: "/downstream_service"
              route:
                cluster: reverse_connection_cluster  # Routes to initiator via reverse tunnel
        http_filters:
        # Lua filter processes headers and sets computed host ID
        - name: envoy.filters.http.lua
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.lua.v3.Lua
            inline_code: |
              function envoy_on_request(request_handle)
                local headers = request_handle:headers()
                local node_id = headers:get("x-node-id")
                local cluster_id = headers:get("x-cluster-id")
                local host_header = headers:get("host")

                local host_id = ""

                -- Priority 1: x-node-id header
                if node_id then
                  host_id = node_id
                  request_handle:logInfo("Using x-node-id as host_id: " .. host_id)
                -- Priority 2: x-cluster-id header
                elseif cluster_id then
                  host_id = cluster_id
                  request_handle:logInfo("Using x-cluster-id as host_id: " .. host_id)
                -- Priority 3: Extract UUID from Host header (uuid.example.domain)
                elseif host_header then
                  local uuid = string.match(host_header, "^([^%.]+)%.example%.domain$")
                  if uuid then
                    host_id = uuid
                    request_handle:logInfo("Extracted UUID from Host header as host_id: " .. host_id)
                  else
                    request_handle:logError("Host header format invalid. Expected: uuid.example.domain, got: " .. host_header)
                    -- Don't set x-computed-host-id, which will cause cluster matching to fail
                    return
                  end
                else
                  request_handle:logError("No valid headers found: x-node-id, x-cluster-id, or Host")
                  -- Don't set x-computed-host-id, which will cause cluster matching to fail
                  return
                end

                -- Set the computed host ID for the reverse connection cluster
                headers:add("x-computed-host-id", host_id)
              end
        - name: envoy.filters.http.router
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

The example above demonstrates using a :ref:`Lua filter <config_http_filters_lua>` to implement flexible
header-based routing logic. This is one of several approaches for computing the ``host_id`` from request
context; alternatives include using other HTTP filters, the ``host_id_format`` field with direct header
mapping, or custom filter implementations. The Lua filter checks multiple headers in priority order and
sets the ``x-computed-host-id`` header, which the reverse connection cluster uses to look up the appropriate
tunnel connection.

The header priority order is:

1. **x-node-id header**: Highest priority—uses the value directly
2. **x-cluster-id header**: Fallback—used when x-node-id is not present
3. **Host header**: Second fallback—extracts UUID from format ``uuid.example.domain``
4. **None found**: Logs error and fails cluster matching

**Example request flows:**

1. **Request with node ID** (highest priority):

   .. code-block:: http

      GET /downstream_service HTTP/1.1
      x-node-id: example-node

   The filter sets ``host_id = "example-node"``.

2. **Request with cluster ID** (fallback):

   .. code-block:: http

      GET /downstream_service HTTP/1.1
      x-cluster-id: example-cluster

   The filter sets ``host_id = "example-cluster"``.

3. **Request with Host header** (second fallback):

   .. code-block:: http

      GET /downstream_service HTTP/1.1
      Host: example-uuid.example.domain

   The filter extracts the UUID and sets ``host_id = "example-uuid"``.

.. _config_reverse_connection_security:

Security Considerations
-----------------------

Reverse tunnels should be used with appropriate security measures:

* **Authentication**: Implement proper authentication mechanisms for handshake validation as part of the reverse tunnel handshake protocol.
* **Authorization**: Validate that downstream nodes are authorized to connect to upstream clusters.
* **TLS**: TLS can be configured for each upstream cluster that reverse tunnels are established to.
