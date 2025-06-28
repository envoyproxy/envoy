# Life of a Request

This document describes the complete lifecycle of a request through the reverse connection system, from initial request to final response.

## Overview Diagram

![Life of a Request Diagram](https://lucid.app/lucidchart/c369b0df-436b-4dbb-b32b-03d2412c1db2/edit?invitationId=inv_1aa0e150-484c-4b43-9799-afb3d61289e1&page=IjcZ1O4Q5TdRF#)

## Request Lifecycle

### 1. Request with Custom Headers

The request begins with custom headers that identify the target downstream cluster and node:

```http
GET /api/data HTTP/1.1
Host: reverse-connection-cluster
x-dst-cluster-uuid: cluster-123
x-remote-node-id: node-456
```

### 2. Cluster Processing

The request lands on a `reverse_connection` cluster configured in Envoy:

```yaml
    clusters:
    - name: reverse_connection_cluster
      connect_timeout: 2s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3alpha.RevConClusterConfig
          # The following headers are expected in downstream requests
          # to be sent over reverse connections
          http_header_names:
            - x-remote-node-id # Should be set to the node ID of the downstream envoy node, ie., on-prem-node
            - x-dst-cluster-uuid # Should be set to the cluster ID of the downstream envoy node, ie., on-prem
```

The cluster extracts the custom headers and creates a host with the appropriate metadata. This is important because when the subsequent requests arrive with the same node header, the same host will be re-used, thereby re-using the tcp connection pool and the cached socket.

### 3. Host Address Creation

The host is created with a custom address that returns the `UpstreamReverseSocketInterface` in its `socketInterface()` method:

### 4. Thread-Local Socket Creation

The `socket()` function of `UpstreamReverseSocketInterface` is called thread-locally:


### 5. Socket Selection from Thread-Local SocketManager

The `UpstreamReverseSocketInterface` picks an available socket from the thread-local `SocketManager`:


### 6. Cluster -> Node Mapping

The `SocketManager` maintains two key mappings:

- **Cluster → Node Mapping**: `cluster_id -> set<node_ids>` - tracks which nodes belong to each cluster
- **Node → Socket Mapping**: `node_id -> cached_socket` - stores the actual cached socket for each node

**Request Routing Logic:**
- **Node-specific requests**: Use `x-remote-node-id` header to route to specific node socket
- **Cluster requests**: Use `x-dst-cluster-uuid` header to randomly select from available nodes in that cluster

### 7. UpstreamReverseConnectionIOHandle Creation

The `UpstreamReverseSocketInterface` returns an `UpstreamReverseConnectionIOHandle` that wraps the cached socket:


### 8. Connection Creation and Usage

A connection is created with the `UpstreamReverseConnectionIOHandle` and used for the request:


## References
- [Reverse Connection Design Document](https://docs.google.com/document/d/1rH91TgPX7JbTcWYacCpavY4hiA1ce1yQE4mN3XZSewo/edit?tab=t.0) 