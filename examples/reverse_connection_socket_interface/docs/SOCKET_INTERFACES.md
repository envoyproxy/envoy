# Socket Interfaces

## Reverse Tunnel Initiator

This document explains how the ReverseTunnelInitiator works, including thread-local entities and the reverse connection establishment process.

## Overview

The ReverseTunnelInitiator manages the initiation of reverse connections from on-premises Envoy instances to cloud-based instances. It uses thread-local storage to manage connection pools and handles the establishment of reverse TCP connections.

## Sequence Diagram

The following diagram shows the flow from ListenerFactory to reverse connection establishment:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Initiator Side                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┐    ┌─────────────────────┐    ┌─────────────────────┐ │
│  │ ListenerFactory │    │ ReverseTunnel       │    │   Worker Thread     │ │
│  │                 │    │ Initiator           │    │                     │ │
│  │ • detects       │───▶│                     │───▶│ • socket() called   │ │
│  │   ReverseConn   │    │ • registered as     │    │ • creates           │ │
│  │   Address       │    │   bootstrap ext     │    │   ReverseConnIO     │ │
│  │                 │    │ • handles socket    │    │   Handle            │ │
│  └─────────────────┘    │   creation          │    │                     │ │
│                         └─────────────────────┘    └─────────────────────┘ │
│                                                              │              │
│                                                              ▼              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │              ReverseConnectionIOHandle                              │   │
│  │                                                                     │   │
│  │  • Resolves target cluster to get host addresses                   │   │
│  │  • Establishes reverse TCP connections to each host                │   │
│  │  • Triggers listener accept() when connections ready               │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    TCP Connection Establishment                     │   │
│  │                                                                     │   │
│  │  ┌─────────────────────────────────────────────────────────────┐   │   │
│  │  │           HTTP Handshake with Metadata                      │   │   │
│  │  │                                                             │   │   │
│  │  │  HTTP POST with reverse connection metadata                 │   │   │
│  │  │  ────────────────────────────────────────────────────────── │   │   │
│  │  │  Response: ACCEPTED/REJECTED                                │   │   │
│  │  └─────────────────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Upstream Side                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Worker Threads                                   │   │
│  │                                                                     │   │
│  │  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐ │   │
│  │  │   Worker 1      │    │   Worker 2      │    │   Worker N      │ │   │
│  │  │                 │    │                 │    │                 │ │   │
│  │  │ ┌─────────────┐ │    │ ┌─────────────┐ │    │ ┌─────────────┐ │ │   │
│  │  │ │Reverse Conn │ │    │ │Reverse Conn │ │    │ │Reverse Conn │ │ │   │
│  │  │ │HTTP Filter  │ │    │ │HTTP Filter  │ │    │ │HTTP Filter  │ │ │   │
│  │  │ │(Thread-Local)│ │    │ │(Thread-Local)│ │    │ │(Thread-Local)│ │ │   │
│  │  │ │             │ │    │ │             │ │    │ │             │ │ │   │
│  │  │ │• Accepts    │ │    │ │• Accepts    │ │    │ │• Accepts    │ │ │   │
│  │  │ │  incoming   │ │    │ │  incoming   │ │    │ │  incoming   │ │ │   │
│  │  │ │  reverse    │ │    │ │  reverse    │ │    │ │  reverse    │ │ │   │
│  │  │ │  connections│ │    │ │  connections│ │    │ │  connections│ │ │   │
│  │  │ │• Responds   │ │    │ │• Responds   │ │    │ │• Responds   │ │ │   │
│  │  │ │  ACCEPTED/  │ │    │ │  ACCEPTED/  │ │    │ │  ACCEPTED/  │ │ │   │
│  │  │ │  REJECTED   │ │    │ │  REJECTED   │ │    │ │  REJECTED   │ │ │   │
│  │  │ │• Calls      │ │    │ │• Calls      │ │    │ │• Calls      │ │ │   │
│  │  │ │  Upstream   │ │    │ │  Upstream   │ │    │ │  Upstream   │ │ │   │
│  │  │ │  Socket     │ │    │ │  Socket     │ │    │ │  Socket     │ │ │   │
│  │  │ │  Interface  │ │    │ │  Interface  │ │    │ │  Interface  │ │ │   │
│  │  │ └─────────────┘ │    │ └─────────────┘ │    │ └─────────────┘ │ │   │
│  │  └─────────────────┘    └─────────────────┘    └─────────────────┘ │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Worker Threads                                   │   │
│  │                                                                     │   │
│  │  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐ │   │
│  │  │   Worker 1      │    │   Worker 2      │    │   Worker N      │ │   │
│  │  │                 │    │                 │    │                 │ │   │
│  │  │ ┌─────────────┐ │    │ ┌─────────────┐ │    │ ┌─────────────┐ │ │   │
│  │  │ │Reverse Conn │ │    │ │Reverse Conn │ │    │ │Reverse Conn │ │ │   │
│  │  │ │HTTP Filter  │ │    │ │HTTP Filter  │ │    │ │HTTP Filter  │ │ │   │
│  │  │ │(Thread-Local)│ │    │ │(Thread-Local)│ │    │ │(Thread-Local)│ │ │   │
│  │  │ │             │ │    │ │             │ │    │ │             │ │ │   │
│  │  │ │• Accepts    │ │    │ │• Accepts    │ │    │ │• Accepts    │ │ │   │
│  │  │ │  incoming   │ │    │ │  incoming   │ │    │ │  incoming   │ │ │   │
│  │  │ │  reverse    │ │    │ │  reverse    │ │    │ │  reverse    │ │ │   │
│  │  │ │  connections│ │    │ │  connections│ │    │ │  connections│ │ │   │
│  │  │ │• Responds   │ │    │ │• Responds   │ │    │ │• Responds   │ │ │   │
│  │  │ │  ACCEPTED/  │ │    │ │  ACCEPTED/  │ │    │ │  ACCEPTED/  │ │ │   │
│  │  │ │  REJECTED   │ │    │ │  REJECTED   │ │    │ │  REJECTED   │ │ │   │
│  │  │ │• Calls      │ │    │ │• Calls      │ │    │ │• Calls      │ │ │   │
│  │  │ │  Upstream   │ │    │ │  Upstream   │ │    │ │  Upstream   │ │ │   │
│  │  │ │  Socket     │ │    │ │  Socket     │ │    │ │  Socket     │ │ │   │
│  │  │ │  Interface  │ │    │ │  Interface  │ │    │ │  Interface  │ │ │   │
│  │  │ └─────────────┘ │    │ └─────────────┘ │    │ └─────────────┘ │ │   │
│  │  └─────────────────┘    └─────────────────┘    └─────────────────┘ │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │              UpstreamReverseSocketInterface                         │   │
│  │                    (Global)                                         │   │
│  │                                                                     │   │
│  │  • Fetches thread-local SocketManager                               │   │
│  │  • Passes accepted sockets to thread-local SocketManager            │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Thread-Local SocketManagers                     │   │
│  │                                                                     │   │
│  │  ┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐ │   │
│  │  │SocketManager    │    │SocketManager    │    │SocketManager    │ │   │
│  │  │(Worker 1)       │    │(Worker 2)       │    │(Worker N)       │ │   │
│  │  │                 │    │                 │    │                 │ │   │
│  │  │• Caches         │    │• Caches         │    │• Caches         │ │   │
│  │  │  connections    │    │  connections    │    │  connections    │ │   │
│  │  │  per node/      │    │  per node/      │    │  per node/      │ │   │
│  │  │  cluster        │    │  cluster        │    │  cluster        │ │   │
│  │  └─────────────────┘    └─────────────────┘    └─────────────────┘ │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```


## Connection Establishment Process

### 1. Socket Interface Registration

The bootstrap extension registers the custom socket interface:

```yaml
bootstrap_extensions:
- name: envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.bootstrap.reverse_connection_socket_interface.v3alpha.DownstreamReverseConnectionSocketInterface
    stat_prefix: "downstream_reverse_connection"
```

### 2. Thread-Local Socket Creation

When a listener with reverse connection metadata is created:
- ListenerFactory detects ReverseConnectionAddress
- Calls DownstreamReverseSocketInterface::socket()
- Creates ReverseConnectionIOHandle for each worker thread

### 3. Cluster Resolution and Connection Initiation

The ReverseConnectionIOHandle:
- Resolves target cluster to get cluster -> host address mapping
- Establishes reverse TCP connections to each host in the cluster
- Initiates the reverse connection handshake; writes a HTTP POST with the
reverse connection request message on the established TCP connection
- upstream replies with ACCEPTED/REJECTED

### 4. Socket Management

- Once the reverse connection is accepted by upstream, downstream triggers listener accept() mechanism

## Waking up accept() on connection establishment

The reverse connection system uses a trigger pipe mechanism to wake up the `accept()` method when a reverse connection is successfully established. This allows the listener to process established connections as they become available.

### Trigger Pipe Mechanism

The system uses a pipe with two file descriptors:
- `trigger_pipe_read_fd_`: Read end of the pipe, monitored by `accept()`
- `trigger_pipe_write_fd_`: Write end of the pipe, used to signal connection establishment

### Connection Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Connection Establishment Flow                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┐    ┌─────────────────────┐    ┌─────────────────────┐ │
│  │ RCConnection    │    │ onConnectionDone()  │    │ Trigger Pipe        │ │
│  │ Wrapper         │    │                     │    │                     │ │
│  │                 │    │ • Connection        │    │ • Write 1 byte      │ │
│  │ • HTTP handshake│───▶│   established       │───▶│   to write_fd       │ │
│  │ • Success       │    │ • Push connection   │    │ • Triggers EPOLL    │ │
│  │   response      │    │   to queue          │    │   event             │ │
│  └─────────────────┘    └─────────────────────┘    └─────────────────────┘ │
│                                                              │              │
│                                                              ▼              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    accept() Method                                  │   │
│  │                                                                     │   │
│  │  • Blocks on read() from trigger_pipe_read_fd_                     │   │
│  │  • Receives 1 byte when connection ready                           │   │
│  │  • Pops connection from established_connections_ queue             │   │
│  │  • Extracts file descriptor from connection                        │   │
│  │  • Wraps FD in new IoHandle and returns it                        │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                              │                                              │
│                              ▼                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                    Filter Chain Processing                          │   │
│  │                                                                     │   │
│  │  • IoHandle passes through regular filter chain                    │   │
│  │  • Uses FD of previously established connection                    │   │
│  │  • Normal Envoy connection processing                              │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Implementation Details

1. **Connection Establishment**: When a reverse connection handshake succeeds, `onConnectionDone()` is called
2. **Queue Management**: The established connection is pushed to `established_connections_` queue
3. **Trigger Signal**: A single byte is written to `trigger_pipe_write_fd_` to signal readiness
4. **EPOLL Event**: This triggers an EPOLL event that wakes up the event loop
5. **accept() Processing**: The `accept()` method reads the byte from `trigger_pipe_read_fd_`
6. **Connection Retrieval**: The method pops the connection from the queue and extracts its file descriptor
7. **IoHandle Creation**: A new `IoSocketHandleImpl` is created with the connection's file descriptor
8. **Filter Chain**: The IoHandle is returned and processed through the normal Envoy filter chain

This allows us to cleanly cache a previously established connection.

## Reverse Tunnel Acceptor

The ReverseTunnelAcceptor manages accepted reverse connections on the cloud side. It uses thread-local socket managers to maintain connection caches and mappings.

### Thread-Local Socket Management

Each worker thread has its own socket manager that:
- **Node Caching**: Maintains `node_id -> cached_sockets` mapping for connection retrieval
- **Cluster Mapping**: Stores `cluster_id -> node_ids` mappings. This is used to return cached sockets for different nodes in a load balanced fashion for requests intended to a specific downstream cluster.

## References

- [Reverse Connection Design Document](https://docs.google.com/document/d/1rH91TgPX7JbTcWYacCpavY4hiA1ce1yQE4mN3XZSewo/edit?tab=t.0)
