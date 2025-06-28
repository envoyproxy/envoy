# Reverse Connection Listener: Using Custom Resolver

This document explains how reverse connection listeners are configured and how the custom resolver parses reverse connection metadata to initiate reverse tunnels.

## Overview

Reverse connection listeners use a custom address resolver to parse metadata encoded in socket addresses and establish reverse TCP connections. The use of listeners makes reverse tunnel initiation dynamically configurable via LDS updates so that reverse tunnels to clusters can be set up on demand and torn down. It also eases cleanup, which can be done by just draining the listener. 

## Architecture Diagram

The following diagram shows how multiple listeners with reverse connection metadata are processed:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Envoy Configuration                               │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────┐    ┌─────────────────────┐                        │
│  │   Listener 1        │    │   Listener 2        │                        │
│  │                     │    │                     │                        │
│  │ address:            │    │ address:            │                        │
│  │ "rc://node1:        │    │ "rc://node2:        │                        │
│  │  cluster1:          │    │  cluster2:          │                        │
│  │  tenant1@cloud:2"   │    │  tenant2@cloud:1"   │                        │
│  │                     │    │                     │                        │
│  │ resolver_name:      │    │ resolver_name:      │                        │
│  │ "envoy.resolvers.   │    │ "envoy.resolvers.   │                        │
│  │  reverse_connection"│    │  reverse_connection"│                        │
│  └─────────────────────┘    └─────────────────────┘                        │
│           │                           │                                    │
│           └───────────┬───────────────┘                                    │
│                       │                                                    │
│                       ▼                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                ReverseConnectionResolver                            │   │
│  │                                                                     │   │
│  │  • Detects "rc://" prefix                                          │   │
│  │  • Parses metadata: node_id, cluster_id, tenant_id,               │   │
│  │    target_cluster, connection_count                                │   │
│  │  • Creates ReverseConnectionAddress instances                      │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                       │                                                    │
│                       ▼                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │              ReverseConnectionAddress                               │   │
│  │                                                                     │   │
│  │  • Stores parsed configuration                                      │   │
│  │  • Internal address: "127.0.0.1:0" (for filter chain matching)     │   │
│  │  • Logical name: original "rc://" address                          │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                       │                                                    │
│                       ▼                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    ListenerFactory                                  │   │
│  │                                                                     │   │
│  │  • Detects ReverseConnectionAddress                                 │   │
│  │  • Creates DownstreamReverseSocketInterface                         │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                       │                                                    │
│                       ▼                                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Worker Threads                                  │   │
│  │                                                                     │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │   │
│  │  │   Worker 1  │  │   Worker 2  │  │   Worker 3  │  │   Worker N  │ │   │
│  │  │             │  │             │  │             │  │             │ │   │
│  │  │ • Listener  │  │ • Listener  │  │ • Listener  │  │ • Listener  │ │   │
│  │  │   1 and 2   │  │   1 and 2   │  │   1 and 2   │  │   1 and 2   │ │   │
│  │  │   initiate  │  │   initiate  │  │   initiate  │  │   initiate  │ │   │
│  │  │   reverse   │  │   reverse   │  │   reverse   │  │   reverse   │ │   │
│  │  │   tunnels   │  │   tunnels   │  │   tunnels   │  │   tunnels   │ │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘ │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

## How Reverse Connection Listeners Work

### 1. Listener Configuration

Listeners are configured with socket addresses that use the custom resolver and contain reverse connection metadata:

```yaml
listeners:
  - name: reverse_conn_listener_1
    address:
      socket_address:
        address: "rc://node1:cluster1:tenant1@cloud:2"
        port_value: 0
        resolver_name: "envoy.resolvers.reverse_connection"
  
  - name: reverse_conn_listener_2
    address:
      socket_address:
        address: "rc://node2:cluster2:tenant2@cloud:1"
        port_value: 0
        resolver_name: "envoy.resolvers.reverse_connection"
```

### 2. Address Resolution

The `ReverseConnectionResolver` detects addresses starting with `rc://` and parses the metadata:
- **Format**: `rc://src_node_id:src_cluster_id:src_tenant_id@cluster_name:count`
- **Example**: `rc://node1:cluster1:tenant1@cloud:2` parses to:
  - Source node ID: `node1`
  - Source cluster ID: `cluster1`
  - Source tenant ID: `tenant1`
  - Target cluster: `cloud`
  - Connection count: `2`

### 3. ReverseConnectionAddress Creation

The resolver creates a `ReverseConnectionAddress` instance that:
- Stores the parsed reverse connection configuration
- Uses a dummy `127.0.0.1:0` as the address. This needs to be a valid address for filter chain lookups.
- Maintains the original `rc://` address in `logicalName()` for identification

### 4. ListenerFactory Processing

The ListenerFactory detects reverse connection addresses and:
- Creates the appropriate socket interface (DownstreamReverseSocketInterface)

### 5. Socket Interface Integration

The DownstreamReverseSocketInterface (described in a separate document) handles:
- Resolving the target cluster to get host addresses
- Initiating TCP connections
- Managing connection pools and health checks
- Triggering listener accept() when connections are ready

## References

- [Reverse Connection Design Document](https://docs.google.com/document/d/1rH91TgPX7JbTcWYacCpavY4hiA1ce1yQE4mN3XZSewo/edit?tab=t.0)
- [Architecture Diagram](https://lucid.app/lucidchart/daa06383-79a7-454e-9bd6-f2ec3dbb8c2c/edit?invitationId=inv_481cba23-1629-4e0f-ba16-25b87c07fb94&page=OuV8EpgCLfgFX#) 