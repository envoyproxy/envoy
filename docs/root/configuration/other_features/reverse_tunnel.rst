.. _config_reverse_tunnel:

Reverse Tunnels
===============

.. attention::

  The reverse tunnels feature is experimental and is currently under active development.

Envoy supports reverse tunnels that enable establishing persistent connections from downstream Envoy instances
to upstream Envoy instances without requiring the upstream to be directly reachable from the downstream.
This feature is particularly useful in scenarios where downstream instances are behind NATs, firewalls,
or in private networks, and need to initiate connections to upstream instances in public networks or cloud environments.

Reverse tunnels work by having the downstream Envoy initiate TCP connections to upstream Envoy instances
and keep them alive for reuse. These connections are established using a handshake protocol and can be
used for forwarding traffic from services behind upstream Envoy to downstream services behind the downstream Envoy.

.. _config_reverse_tunnel_bootstrap:

Reverse tunnels require the following extensions:

1. **Downstream socket interface**: Registered as a bootstrap extension on initiator Envoy to initiate and maintain reverse tunnels.
2. **Upstream socket interface**: Registered as a bootstrap extension on responder Envoy to accept and manage reverse tunnels.
3. **Reverse tunnel network filter**: On responder Envoy to accept reverse tunnel requests.
4. **Reverse connection cluster**: Added on responder Envoy for each downstream Envoy node that needs to be reached through reverse tunnels.

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

This extension enables the initiator to initiate and manage reverse tunnels to the responder Envoy.

.. _config_reverse_tunnel_listener:

Reverse Tunnel Listener
~~~~~~~~~~~~~~~~~~~~~~~~

The reverse tunnel listener triggers the reverse connection initiation to the upstream Envoy instance and encodes
identity metadata for the local Envoy. It also contains the route configuration for downstream services
reachable via reverse tunnels.

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

**Identifier Definitions:**

* **src_node_id**: A unique identifier for this specific node. Each node must have a unique src_node_id across
  the entire system to ensure proper routing and connection management.
* **src_cluster_id**: An identifier for a logical group of nodes. Multiple nodes can belong to the same cluster,
  allowing data requests to be sent using the src_cluster_id and load balanced between nodes within that cluster.
* **src_tenant_id**: Used in multi-tenant environments to isolate traffic and resources between different tenants
  or organizational units.

**Identifier Rules:**

* Each node should have a unique src_node_id.
* The src_cluster_id should not be the same as any src_node_id.
* Nodes in the same cluster share the same src_cluster_id but have different src_node_id values.

The 'downstream-service' cluster is the service behind initiator Envoy that will be accessed via reverse tunnels
from behind the responder Envoy.

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

Each upstream Envoy to which reverse tunnels should be established needs to be configured with a cluster,
added via CDS.

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

To initiate reverse tunnels to multiple upstream clusters, each such cluster needs to be configured under the additional_addresses section.

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

This extension enables the responder to accept and manage reverse connections from initiator Envoys.

.. _config_reverse_tunnel_network_filter:

Reverse Tunnel Network Filter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``envoy.filters.network.reverse_tunnel`` network filter implements the reverse tunnel handshake protocol and accepts or rejects
reverse tunnel requests:

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

Each downstream node reachable from upstream Envoy via reverse connections needs to be configured with a
reverse connection cluster. When a data request arrives at the upstream Envoy, this cluster uses cached
reverse connections instead of creating new forward connections.

For each such data request, a "host_id" is specified or computed from the request headers; this "host_id"
represents the downstream node that the request should be routed to.

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

The reverse connection cluster configuration specifies:

* **Load balancing policy**: ``CLUSTER_PROVIDED`` allows the custom cluster to manage load balancing
* **Host ID Format**: Uses Envoy's formatter system to extract the target downstream node identifier from
  request context. The ``host_id_format`` field supports:

  - ``%REQ(header-name)%``: Extract value from request header
  - ``%DYNAMIC_METADATA(namespace:key)%``: Extract value from dynamic metadata
  - ``%FILTER_STATE(key)%``: Extract value from filter state
  - ``%DOWNSTREAM_REMOTE_ADDRESS%``: Use downstream connection address
  - Plain text and combinations of the above

An example of how to process headers and set the host ID is described in the
:ref:`config_reverse_connection_egress_listener` section.

* **Protocol**: Only HTTP/2 is supported for reverse connections.
* **Host Reuse**: Once a host is created for a specific downstream node ID, it is cached and reused for all
  subsequent requests to that node. Therefore, each data request is multiplexed as a new stream on the existing
  HTTP/2 connection.

.. _config_reverse_connection_egress_listener:

Egress Listener for Data Traffic
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add an egress listener on upstream Envoy that accepts data traffic and routes it to the reverse connection
cluster. This listener includes header processing logic to determine the target downstream node or "host_id"
to use for the data request.

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

The egress listener includes a Lua filter as an example of how to implement flexible header-based routing
to determine which downstream node to route requests to. This is just one of many possible approaches for
computing the "host_id" from request headers—other options include using different HTTP filters,
custom formatters, or direct header mapping. The Lua filter example checks multiple headers sequentially
and sets a computed host ID for the reverse connection cluster, which is then used to look up a socket.

In the example above, the following headers are checked in order of priority:

1. **x-node-id header**: Highest priority—uses the value directly
2. **x-cluster-id header**: Fallback—used when x-node-id is not present
3. **Host header**: Second fallback—extracts UUID from format ``uuid.example.domain``
4. **None found**: Logs error and fails cluster matching

Example Request Flow:

1. **Request with node ID**:

   .. code-block:: http

     GET /downstream_service HTTP/1.1
     x-node-id: example-node

   Result: ``host_id = "example-node"``

2. **Request with cluster ID** (fallback):

   .. code-block:: http

     GET /downstream_service HTTP/1.1
     x-cluster-id: example-cluster

   Result: ``host_id = "example-cluster"``

3. **Request with Host header** (second fallback):

   .. code-block:: http

     GET /downstream_service HTTP/1.1
     Host: example-uuid.example.domain

   Result: ``host_id = "example-uuid"``

.. _config_reverse_connection_security:

Security Considerations
-----------------------

Reverse tunnels should be used with appropriate security measures:

* **Authentication**: Implement proper authentication mechanisms for handshake validation as part of the reverse tunnel handshake protocol.
* **Authorization**: Validate that downstream nodes are authorized to connect to upstream clusters.
* **TLS**: TLS can be configured for each upstream cluster that reverse tunnels are established to.
