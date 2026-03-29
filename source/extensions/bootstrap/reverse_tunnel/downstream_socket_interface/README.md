# Downstream reverse tunnel socket interface

This directory implements the **initiator** (downstream) side of Envoy’s reverse tunnel feature. It allows a downstream Envoy to establish and maintain outbound TCP connections to upstream Envoy instances so that traffic can flow “back” through those connections without the upstream needing to reach the downstream directly.

## Role of `ReverseConnectionIOHandle`

`ReverseConnectionIOHandle` is the core type that implements the **listen socket** for the reverse-tunnel listener on the initiator. It does not behave like a normal listen socket: it does not bind to a port and accept incoming connections. Instead it:

1. **Initiates** outbound TCP connections to each host in the configured upstream cluster(s).
2. **Performs a handshake** on each connection (via `RCConnectionWrapper`).
3. **Exposes those connections as “accepted”** connections when the listener calls `accept()`.

So from the listener’s point of view it still “accepts” connections; in reality those connections were created by this handle and enqueued after a successful handshake.

---

## How the class is used in the reverse tunnel flow

### 1. Configuration and address resolution

- The user configures a listener with an `rc://` address, e.g.  
  `rc://src_node_id:src_cluster_id:src_tenant_id@remote_cluster:connection_count`
- The bootstrap extension **ReverseTunnelInitiator** (downstream socket interface) is registered so that the socket interface used for creating listen sockets is the reverse-tunnel one.
- When Envoy resolves listener addresses, `ReverseConnectionAddress` is used for `rc://` addresses (with a synthetic `127.0.0.1:0` for compatibility). That address carries the parsed config (node id, cluster id, tenant id, remote cluster name, connection count).

### 2. Creating the “listen” socket (one per worker)

- When the listener manager creates listen sockets for the `rc://` listener, it calls the socket interface’s `socket()` with the resolved `ReverseConnectionAddress`.
- **ReverseTunnelInitiator::socket()** detects the reverse connection address and calls **createReverseConnectionSocket()**, which:
  - Creates a plain TCP socket fd (`::socket()`).
  - Wraps it in a **ReverseConnectionIOHandle** with:
    - The fd
    - **ReverseConnectionSocketConfig** (from the `rc://` address: node id, cluster id, tenant id, remote cluster, connection count, etc.)
    - **ClusterManager** (to resolve upstream cluster hosts)
    - **ReverseTunnelInitiatorExtension** (for stats and scope)
    - **Stats::Scope** (for metrics)
- The listener factory creates one such socket for worker 0, then **duplicates** it for workers 1..N via `Socket::duplicate()` → `io_handle_->duplicate()`.

### 3. Why `duplicate()` is overridden (#43270)

- Without an override, `ReverseConnectionIOHandle` would use the base **IoSocketHandleImpl::duplicate()**, which returns a **plain** IoHandle (duplicated fd only), not a `ReverseConnectionIOHandle`.
- Then only worker 0’s socket would be a `ReverseConnectionIOHandle`; workers 1..N would have a normal handle and would never run the reverse-connection logic.
- **ReverseConnectionIOHandle::duplicate()** is overridden to return a **new ReverseConnectionIOHandle** with a duplicated fd and the same config, cluster manager, and extension. That way **every worker** gets a full `ReverseConnectionIOHandle` and will start reverse connections (see below).

### 4. Listener startup and `initializeFileEvent()`

- For each worker, the listener implementation (e.g. **TcpListenerImpl**) is constructed with that worker’s listen socket (the `ReverseConnectionIOHandle` or its duplicate).
- When the listener is enabled, it calls **socket_->ioHandle().initializeFileEvent(dispatcher, ...)** on the worker thread.
- **ReverseConnectionIOHandle::initializeFileEvent()** is where the reverse-connection workflow actually starts on that worker:
  - Creates a **trigger pipe** (read/write fds) used to wake up `accept()` when a new connection is ready.
  - Replaces the monitored fd with the **pipe read fd** (so the dispatcher watches the pipe, not the original socket fd).
  - Schedules **maintainReverseConnections()** (once immediately and then on a retry timer).
  - Calls the base **IoSocketHandleImpl::initializeFileEvent()** so the dispatcher monitors the pipe.

So each worker that has a `ReverseConnectionIOHandle` will run this logic and start maintaining reverse connections.

### 5. Maintaining reverse connections

- **maintainReverseConnections()** iterates over the configured remote clusters and, for each cluster, calls **maintainClusterConnections(cluster_name, cluster_config)**.
- For each cluster it:
  - Uses **ClusterManager** to get the set of resolved hosts.
  - For each host, ensures there are at least `reverse_connection_count` established (and handed off) connections; if not, it calls **initiateOneReverseConnection()** to open a new TCP connection to that host.
- **initiateOneReverseConnection()**:
  - Creates a client connection to the host (via the cluster manager / connection pool path).
  - Wraps it in **RCConnectionWrapper**, which runs the HTTP handshake (identity and metadata) toward the upstream Envoy.
  - On success, the wrapper calls **onConnectionDone()** on the handle.

### 6. Handshake completion and “accept”

- When **RCConnectionWrapper** finishes the handshake successfully, it calls **ReverseConnectionIOHandle::onConnectionDone(error, wrapper, closed)** with an empty `error`.
- The handle:
  - Pushes the underlying **ClientConnection** into **established_connections_**.
  - Writes a byte to the **trigger pipe write fd** to wake the dispatcher.
- The dispatcher is monitoring the **pipe read fd** (see step 4). When it becomes readable, it runs the listener’s callback, which eventually calls **accept()** on the same IoHandle.

### 7. `accept()` behavior

- **ReverseConnectionIOHandle::accept()** does not call `::accept()` on the original socket. Instead it:
  - Reads a byte from the trigger pipe (to clear the wake-up).
  - Pops the next connection from **established_connections_**.
  - Fills in the client address from that connection (for logging/routing).
  - Returns an IoHandle that represents that established reverse connection (e.g. the connection’s socket wrapped for use as an accepted connection).

So the listener sees a stream of “accepted” connections that are actually the outbound reverse connections that this handle established and handed off after handshake.

### 8. Other overrides

- **listen()**: No-op (no real bind/listen).
- **read() / write()**: Delegate or special-case as needed for the trigger pipe and any internal state.
- **close()**: Cleans up the trigger pipe, timers, connection wrappers, and the original socket fd.
- **connect()**: No-op for the listen-side handle (connects are done in **initiateOneReverseConnection()**).

---

## Per-worker behavior and shared config

- **Config** (node id, cluster id, remote cluster, connection count, etc.) is the same for all workers; it comes from the `rc://` address.
- **ClusterManager** and **ReverseTunnelInitiatorExtension** are process-wide; the handle holds references.
- **Per-worker state** includes:
  - The trigger pipe (created in **initializeFileEvent()** on that worker).
  - **rev_conn_retry_timer_** (per handle, so per worker).
  - **connection_wrappers_**, **established_connections_**, **host_to_conn_info_map_**, etc., so each worker maintains its own set of outbound connections and hands them off via its own **accept()**.

So each worker runs an independent reverse-connection maintenance loop and feeds its own established connections into the listener as “accepted” connections.

---

## Summary diagram

```
User config (rc:// listener)
        ↓
ReverseConnectionAddress (resolved from rc://)
        ↓
ListenerManager creates listen sockets (concurrency = N workers)
        ↓
SocketInterface (ReverseTunnelInitiator) → socket(ReverseConnectionAddress)
        ↓
createReverseConnectionSocket() → new ReverseConnectionIOHandle(fd, config, ...)
        ↓
Worker 0: that handle.  Workers 1..N: handle.duplicate() → new ReverseConnectionIOHandle(dup_fd, same config)
        ↓
Each worker: TcpListenerImpl(ReverseConnectionIOHandle) → initializeFileEvent(dispatcher, ...)
        ↓
ReverseConnectionIOHandle::initializeFileEvent()
  - createTriggerPipe(), replace fd with pipe read fd
  - maintainReverseConnections() (and retry timer)
        ↓
maintainReverseConnections() → maintainClusterConnections() → initiateOneReverseConnection()
        ↓
TCP connect to upstream host → RCConnectionWrapper (handshake) → onConnectionDone()
        ↓
Push connection into established_connections_, write to trigger pipe
        ↓
Dispatcher wakes → listener callback → accept()
        ↓
ReverseConnectionIOHandle::accept() → pop from established_connections_, return IoHandle for that connection
```

This is how **ReverseConnectionIOHandle** is used in the reverse tunnel connection feature: it is the initiator-side “listen” socket that actually creates outbound connections, runs the handshake, and presents them to the listener as accepted connections.
