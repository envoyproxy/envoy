# Reverse Tunnels

Reverse tunnels enable establishing persistent connections from downstream Envoy instances to upstream Envoy instances without requiring the upstream to be directly reachable from the downstream. This is particularly useful when downstream instances are behind NATs, firewalls, or in private networks.

## Configuration files

- [`initiator-envoy.yaml`](initiator-envoy.yaml): Configuration for the initiator Envoy (downstream)
- [`responder-envoy.yaml`](responder-envoy.yaml): Configuration for the responder Envoy (upstream)

## Initiator configuration (downstream Envoy)

The initiator Envoy requires the following configuration components:

### Bootstrap extension for socket interface

```yaml   
    bootstrap_extensions:
    - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface
        stat_prefix: "downstream_reverse_connection"
```

This extension enables the initiator to initiate and manage reverse tunnels to the responder Envoy.

### Reverse tunnel listener

The reverse tunnel listener triggers the reverse connection initiation to the upstream Envoy instance and encodes identity metadata for the local Envoy. It also contains the route configuration for downstream services reachable via reverse tunnels.

```yaml
    - name: reverse_conn_listener
  listener_filters_timeout: 0s
  listener_filters:
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
```

The special `rc://` address format encodes:
- `src_node_id`: "downstream-node" - Unique identifier for this downstream node
- `src_cluster_id`: "downstream-cluster" - Cluster name of the downstream Envoy
- `src_tenant_id`: "downstream-tenant" - Tenant identifier
- `remote_cluster`: "upstream-cluster" - Name of the upstream cluster to connect to
- `connection_count`: "1" - Number of reverse connections to establish

### Upstream Cluster

The upstream cluster configuration defines where reverse tunnels should be initiated:

```yaml
- name: upstream-cluster
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
```

### Downstream Service for Reverse Tunnel Data

The downstream service represents the service behind the initiator Envoy that should be reachable via reverse tunnels:

```yaml
- name: downstream-service
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
```

## Responder configuration (upstream Envoy)

The responder Envoy requires the following configuration components:

### Bootstrap extension for socket interface

```yaml
bootstrap_extensions:
- name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface
    stat_prefix: "upstream_reverse_connection"
```

This extension enables the responder to accept and manage reverse connections from initiator Envoys.

### Reverse tunnel filter and listener

```yaml
- name: rev_conn_api_listener
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
```

The `envoy.filters.network.reverse_tunnel` network filter handles the reverse tunnel handshake protocol and connection acceptance.

### Reverse connection cluster

Each downstream node reachable from upstream Envoy via reverse connections needs to be configured with a reverse connection cluster. When a data request arrives at the upstream Envoy, this cluster uses cached "reverse connections" instead of creating new forward connections.

```yaml
- name: reverse_connection_cluster
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
```

### Egress listener for data traffic

The egress listener receives data traffic on the upstream Envoy and routes it to the reverse connection cluster:

```yaml
- name: egress_listener
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 8085  # Port for sending requests to initiator services
  filter_chains:
    - filters:
        - name: envoy.http_connection_manager
          typed_config:
            route_config:
              virtual_hosts:
                - name: backend
                  domains: ["*"]
                  routes:
                    - match:
                        prefix: "/downstream_service"
                      route:
                        cluster: reverse_connection_cluster  # Routes to initiator via reverse tunnel
```

This is the egress listener that receives data traffic on upstream envoy and routes it to the reverse connection cluster.

## How It Works

1. **Tunnel Establishment**: The initiator Envoy establishes reverse tunnels to the responder Envoy on port 9000.
2. **Service Access**: When a request comes to the responder's egress listener (port 8085) for `/downstream_service`, it's routed through to the reverse connection cluster. Instead of creating forward connections to downstream-envoy, a cached "reverse connection" is picked and the data request is routed through it.
3. **Header-Based Routing**: The reverse connection cluster uses `x-remote-node-id` and `x-dst-cluster-uuid` headers to identify which cached reverse connection to use.
4. **Service Response**: The request travels through the reverse tunnel to the initiator, gets routed to the local service, and the response travels back through the same tunnel.