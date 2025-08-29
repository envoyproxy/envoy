.. _config_reverse_connection:

Reverse Connection
------------------

Envoy supports reverse connections that enable re-using existing connectionns to access services behind a private network from behind a public network. This feature is designed to solve the challenge of accessing downstream services in private networks from applications behind a firewall or NAT.

Background
----------

The following is an environment where reverse connections are used:

* There are services behind downstream Envoy instances in a private network.
* There are services behind upstream Envoy instances in a public network. These services cannot access the services behind the downstream Envoy instances using forward connections but need to send requests to them using reverse connections.
* Downstream envoys initiate HTTPS connections to upstream envoy instances, following which upstream envoy caches the connection socket -> these are "reverse connections".
* When a request for a downstream service is received, the upstream Envoy picks an available "reverse connection" or cached connection socket for the downstream cluster and uses it to send the request.

.. image:: /_static/reverse_connection_concept.png
   :alt: Reverse Connection Architecture
   :align: center

Reverse Connection Workflow
---------------------------

The following sequence diagram illustrates the workflow for establishing and managing reverse connections:

.. image:: /_static/reverse_connection_workflow.png
   :alt: Reverse Connection Workflow
   :align: center

**Workflow Steps:**

1. **Create Reverse Connection Listener**: On downstream envoy, reverse connections are initiated by the addition of a reverse connection listener via a LDS update. This makes it easy to pass metadata identifying source Envoy and the remote clusters and reverse tunnel count to each cluster. The upstream clusters are dynamically configurable via CDS.
2. **Initiate Reverse Connections**: The listener calls the reverse connection workflow and initiates raw TCP connections to upstream clusters. This triggers the reverse connection handshake where downstream Envoy should passes metadata identifying itself (node ID, cluster ID) in the reverse connection request. Upstream Envoy will use this to index and store sockets for each downstream node ID by node ID.
3. **Map Connections**: Upstream Envoy accepts the reverse connection handshake and stores the TCP socket mapped to the downstream node ID.
4. **Keepalive**: Reverse connections are long lived connections between downstream and upstream Envoy. Once established, there is a keepalive mechanism to detect connection closure.
6. **Request Routing**: When upstream envoy receives a request that needs to be sent to a downstream service, specific headers indicate which downstream node the request needs to be sent to. Upstream envoy picks a cached socket for the downstream node and sends the request over it.
7. **Connection Closure and Re-initiation**: If a cached reverse connection socket closes on either downstream or upstream envoy, envoy detects it and downstream envoy re-initiates the reverse connection.

Configuration
-------------

Reverse connections require different configurations on downstream (on-prem) and upstream (cloud) Envoy instances. The following sections describe the required components for each side.

Configuration Required on Downstream (On-Prem Envoy)
-------------------------------------------------------

The downstream Envoy instance initiates reverse connections to upstream clusters. A complete example configuration can be found :repo:`here <configs/reverse_connection/onprem-envoy.yaml>`.

**Downstream Socket Interface**

The downstream socket interface is a bootstrap extension that instantiates necessary components for reverse connection initiation on downstream envoy.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
      stat_prefix: "downstream_reverse_connection"

**Reverse Connection Listener**

Reverse Connections are initiated by the addition of a listener on downstream envoy. The reverse connection listener uses a special address format to encode reverse connection metadata, indicating the local identifiers, and the upstream cluster to which reverse connections need to be initiated, and how many need to be initiated. The local identifiers are crucial as upstream envoy uses them to index and store sockets for each downstream node ID by node ID.

.. validated-code-block:: yaml
  :type-name: envoy.config.listener.v3.Listener

  - name: reverse_conn_listener
    listener_filters_timeout: 0s
    listener_filters:
    - name: envoy.filters.listener.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.listener.reverse_connection.v3.ReverseConnection
        ping_wait_timeout: 10
    address:
      socket_address:
        address: "rc://node-id:cluster-id:tenant-id@remote-cluster:connection-count"
        port_value: 0
        resolver_name: "envoy.resolvers.reverse_connection"

The address format `rc://` encodes:
- `node-id`: Source node identifier
- `cluster-id`: Source cluster identifier  
- `tenant-id`: Source tenant identifier
- `remote-cluster`: Target upstream cluster name
- `connection-count`: Number of reverse connections to establish

**Reverse Connection Handshake**

The addition of the reverse connection listener triggers a handshake process between downstream and upstream Envoy instances. The downstream Envoy initiates TCP connections to each host of the upstream cluster, and writes the handshake request on it over HTTP/1.1 POST.

The handshake request contains a protobuf message with node identification metadata:

.. validated-code-block:: yaml
  :type-name: envoy.extensions.bootstrap.reverse_connection_handshake.v3.ReverseConnHandshakeArg

  POST /reverse_connections/request HTTP/1.1
  Host: {upstream_host}
  Accept: */*
  Content-length: {protobuf_size}

  {protobuf_body}

The protobuf message contains:
- `tenant_uuid`: Source tenant identifier
- `cluster_uuid`: Source cluster identifier  
- `node_uuid`: Source node identifier

Upstream Envoy validates whether the request contains the node identifier, and then sends an HTTP response indicating where the reverse connection is accepted or rejected.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.bootstrap.reverse_connection_handshake.v3.ReverseConnHandshakeRet

  HTTP/1.1 200 OK
  Content-Type: application/octet-stream
  Content-Length: {protobuf_size}
  Connection: close

  {protobuf_body}

The response protobuf contains:
- `status`: ACCEPTED or REJECTED
- `status_message`: Optional error message if rejected

**Reverse Connection Listener Filter**

The reverse connection listener filter on downstream envoy owns the socket after the handshake is complete and before data is received on it. It is responsible for replying to TCP keepalives on the socket, and mark the socket dead if replies are not received within a timeout.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.listener.reverse_connection.v3.ReverseConnection

  listener_filters:
  - name: envoy.filters.listener.reverse_connection
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.reverse_connection.v3.ReverseConnection
      ping_wait_timeout: 10

Configuration Required on Upstream (Cloud Envoy)
-----------------------------------------------

The upstream Envoy instance instantiates components that accept and manage reverse connections from downstream instances. A complete example configuration can be found :repo:`here <configs/reverse_connection/cloud-envoy.yaml>`.

**Upstream Socket Interface**

The upstream socket interface is configured via bootstrap extensions and enables the Envoy instance to accept and manage reverse connections from downstream instances.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface

  bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
      stat_prefix: "upstream_reverse_connection"

**Reverse Connection HTTP Filter**

The reverse connection HTTP filter on upstream envoy is responsible for accepting reverse connection handshake from downstream envoy and passing the socket to the upstream socket inteface.
It also exposes the reverse connection API endpoint exposing details like the list of connected clusters via reverse connections.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.filters.http.reverse_conn.v3.ReverseConn

  - name: envoy.filters.http.reverse_conn
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.reverse_conn.v3.ReverseConn
      ping_interval: 2

**Reverse Connection Cluster**

On upstream envoy, any downstream node that needs to be reached via reverse connection needs to be added as a REVERSE_CONNECTION cluster. Requests to such a node need to be made with:
- Special headers set as indicated in the REVERSE_CONNECTION cluster configuration. By default, the headers are:
  - x-remote-node-id: Downstream node ID
  - x-dst-cluster-uuid: Downstream cluster ID
- Host Header set to the downstream node/cluster ID
- SNI set to the downstream node ID

The REVERSE_CONNECTION cluster checks for the uuid in the above sequence, and if found, interfaces with the upstream socket interface and ensures that a cached socket is used to service the request.

.. validated-code-block:: yaml
  :type-name: envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig

  - name: reverse_connection_cluster
    connect_timeout: 200s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        http_header_names:
          - x-remote-node-id      # Downstream node ID
          - x-dst-cluster-uuid   # Downstream cluster ID

**Runtime Configuration**

Enable the following reverse connection on upstream envoy to ensure that it sends a response immediately to the reverse connection handshake request.

.. code-block:: yaml

  layered_runtime:
    layers:
    - name: layer
      static_layer:
        envoy.reloadable_features.reverse_conn_force_local_reply: true


