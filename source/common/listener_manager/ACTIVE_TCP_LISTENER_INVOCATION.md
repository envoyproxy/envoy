# How ActiveTcpListener Is Invoked

**File:** `source/common/listener_manager/active_tcp_listener.h` / `.cc`
**Purpose:** Detailed explanation of how `ActiveTcpListener` gets created and invoked

---

## Overview

`ActiveTcpListener` is created once per listener per worker thread. It acts as the callback handler for TCP accept events and manages all connections for that listener on that worker thread.

**Key Concept:** `ActiveTcpListener` implements `Network::TcpListenerCallbacks` interface, which means it receives callbacks from `TcpListenerImpl` when network events occur.

---

## Table of Contents

1. [Creation Flow](#1-creation-flow)
2. [Event Loop Registration](#2-event-loop-registration)
3. [onAccept Invocation](#3-onaccept-invocation)
4. [Other Method Invocations](#4-other-method-invocations)
5. [Connection Balancing Path](#5-connection-balancing-path)
6. [Config Update Path](#6-config-update-path)
7. [Complete Invocation Timeline](#7-complete-invocation-timeline)

---

## 1. Creation Flow

### 1.1 Main Thread: Listener Configuration

**When:** Listener is added to `ListenerManagerImpl` (bootstrap config or LDS update)

**Steps:**

1. `ListenerManagerImpl::addOrUpdateListener()` processes listener proto config
2. Creates `ListenerImpl` via `ListenerImpl::create()`
3. `ListenerImpl` moves to `warming_listeners_` map
4. After warming completes, moves to `active_listeners_` map
5. `ListenerManagerImpl::addListenerToWorker()` is called

**Code in `listener_manager_impl.cc`:**
```cpp
void ListenerManagerImpl::addListenerToWorker(Worker& worker, ListenerImpl& listener) {
    worker.addListener(
        listener,  // pass listener config
        [this](bool success) { /* completion callback */ }
    );
}
```

### 1.2 Worker Thread: ActiveTcpListener Creation

**When:** Worker thread receives `addListener` message from main thread

**Steps:**

1. Main thread posts task to worker's dispatcher queue
2. Worker thread's event loop picks up posted task
3. `WorkerImpl::addListener()` runs on worker thread
4. Calls `ConnectionHandlerImpl::addListener(config, runtime, random)`
5. **Inside `ConnectionHandlerImpl::addListener()`:**
   - Creates `ActiveListenerDetails` container
   - Checks listener type (TCP stream vs UDP vs internal)
   - **For TCP stream listeners:**
     - Loops through each listen socket factory (usually 1, can be multiple for multi-address)
     - **Creates `ActiveTcpListener` instance:**
       ```cpp
       auto active_tcp_listener = std::make_unique<ActiveTcpListener>(
           *this,                       // ConnectionHandlerImpl reference
           config,                      // ListenerConfig
           runtime,                     // Runtime::Loader
           random,                      // Random::RandomGenerator
           socket_factory->getListenSocket(worker_index_),  // Socket
           address,                     // Listen address
           config.connectionBalancer(*address),  // ConnectionBalancer
           overload_state               // Overload manager state
       );
       ```
   - Stores `ActiveTcpListener` in `listener_map_by_tag_` and `tcp_listener_map_by_address_`

### 1.3 Inside ActiveTcpListener Constructor

**Constructor call chain:**

```cpp
ActiveTcpListener::ActiveTcpListener(
    Network::TcpConnectionHandler& parent,
    Network::ListenerConfig& config,
    Runtime::Loader& runtime,
    Random::RandomGenerator& random,
    Network::SocketSharedPtr&& socket,
    Network::Address::InstanceConstSharedPtr& listen_address,
    Network::ConnectionBalancer& connection_balancer,
    ThreadLocalOverloadStateOptRef overload_state)
: OwnedActiveStreamListenerBase(
    parent,
    parent.dispatcher(),
    parent.createListener(std::move(socket), *this, runtime, random, config, overload_state),
    config),
  tcp_conn_handler_(parent),
  connection_balancer_(connection_balancer),
  listen_address_(listen_address)
{
    connection_balancer_.registerHandler(*this);
}
```

**Critical:** `parent.createListener(...)` passes `*this` as the callback handler to `TcpListenerImpl`

### 1.4 TcpListenerImpl Creation

**Inside `ConnectionHandlerImpl::createListener()`:**

```cpp
Network::ListenerPtr ConnectionHandlerImpl::createListener(
    Network::SocketSharedPtr&& socket,
    Network::TcpListenerCallbacks& cb,  // This is ActiveTcpListener!
    Runtime::Loader& runtime,
    Random::RandomGenerator& random,
    const Network::ListenerConfig& config,
    Server::ThreadLocalOverloadStateOptRef overload_state)
{
    return std::make_unique<Network::TcpListenerImpl>(
        dispatcher_,
        random,
        runtime,
        std::move(socket),
        cb,  // ActiveTcpListener passed as callback handler
        bind_to_port,
        ignore_global_conn_limit,
        bypass_overload_manager,
        max_connections_to_accept_per_socket_event,
        overload_state
    );
}
```

**Result:** `TcpListenerImpl` now holds a reference to `ActiveTcpListener` via `cb_` member variable

---

## 2. Event Loop Registration

### 2.1 File Event Registration

**Inside `TcpListenerImpl` constructor:**

```cpp
TcpListenerImpl::TcpListenerImpl(
    Event::Dispatcher& dispatcher,
    Random::RandomGenerator& random,
    Runtime::Loader& runtime,
    SocketSharedPtr socket,
    TcpListenerCallbacks& cb,  // ActiveTcpListener reference
    bool bind_to_port,
    /* other params */)
: cb_(cb),  // Store reference to ActiveTcpListener
  socket_(socket),
  /* initialize other members */
{
    if (bind_to_port) {
        // Register file descriptor with event loop
        socket_->ioHandle().initializeFileEvent(
            dispatcher,
            [this](uint32_t events) {
                return onSocketEvent(events);  // Lambda captures TcpListenerImpl
            },
            Event::FileTriggerType::Level,  // Level-triggered
            Event::FileReadyType::Read      // Watch for read events
        );
    }
}
```

**What this does:**

- Registers listen socket file descriptor with `epoll`/`kqueue`/etc via dispatcher
- When socket becomes readable (new connection pending), event loop will invoke the lambda
- Lambda calls `TcpListenerImpl::onSocketEvent()`
- Event is **level-triggered** (stays active until all pending connections accepted)

### 2.2 Enable Listening

**After construction, `TcpListenerImpl::enable()` is called:**

```cpp
void TcpListenerImpl::enable() {
    if (bind_to_port_) {
        socket_->ioHandle().enableFileEvents(Event::FileReadyType::Read);
    }
}
```

**Result:** Listener is now actively monitoring the socket for incoming connections

---

## 3. onAccept Invocation

### 3.1 Event Loop Wakes Up

**Timeline:**

1. **Client initiates TCP connection:** SYN packet arrives, kernel completes handshake
2. **Kernel accept queue:** Connection is ready in kernel's listen socket accept queue
3. **Event loop poll:** `epoll_wait()` / `kevent()` returns, indicating listen socket is readable
4. **Dispatcher invokes callback:** Calls the registered lambda from section 2.1

### 3.2 TcpListenerImpl::onSocketEvent()

**Code flow:**

```cpp
absl::Status TcpListenerImpl::onSocketEvent(short flags) {
    ASSERT(flags & Event::FileReadyType::Read);

    uint32_t connections_accepted_from_kernel_count = 0;

    // Accept up to max_connections_to_accept_per_socket_event_ connections
    for (; connections_accepted_from_kernel_count < max_connections_to_accept_per_socket_event_;
         ++connections_accepted_from_kernel_count) {

        if (!socket_->ioHandle().isOpen()) {
            PANIC("listener accept failure");
        }

        sockaddr_storage remote_addr;
        socklen_t remote_addr_len = sizeof(remote_addr);

        // CRITICAL: Call accept() to get new connection from kernel
        IoHandlePtr io_handle = socket_->ioHandle().accept(
            reinterpret_cast<sockaddr*>(&remote_addr),
            &remote_addr_len
        );

        // No more connections ready
        if (io_handle == nullptr) {
            break;
        }

        // Check global connection limit
        if (rejectCxOverGlobalLimit()) {
            io_handle->close();
            cb_.onReject(TcpListenerCallbacks::RejectCause::GlobalCxLimit);
            continue;
        }

        // Check load shedding / overload
        if (shouldShedLoad() || random_.bernoulli(reject_fraction_)) {
            releaseGlobalCxLimitResource();
            io_handle->close();
            cb_.onReject(TcpListenerCallbacks::RejectCause::OverloadAction);
            continue;
        }

        // Get local and remote addresses
        Address::InstanceConstSharedPtr local_address = /* ... */;
        Address::InstanceConstSharedPtr remote_address = /* ... */;

        // INVOKE ActiveTcpListener::onAccept()
        cb_.onAccept(
            std::make_unique<AcceptedSocketImpl>(
                std::move(io_handle),
                local_address,
                remote_address,
                overload_state_,
                track_global_cx_limit_in_overload_manager_
            )
        );
    }

    // Record stats
    cb_.recordConnectionsAcceptedOnSocketEvent(connections_accepted_from_kernel_count);

    return absl::OkStatus();
}
```

**Key Points:**

- Accepts multiple connections per event (batch processing)
- Enforces global connection limit before calling `onAccept`
- Enforces overload/load shedding before calling `onAccept`
- Only calls `onAccept` for connections that pass all checks
- Calls `onReject` to increment stats when connections are rejected

### 3.3 ActiveTcpListener::onAccept()

**Now we're in `ActiveTcpListener`:**

```cpp
void ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
    // Check per-listener connection limit
    if (listenerConnectionLimitReached()) {
        ENVOY_LOG(trace, "closing connection from {}: listener connection limit reached for {}",
                  socket->connectionInfoProvider().remoteAddress()->asString(),
                  config_->name());
        socket->close();
        stats_.downstream_cx_overflow_.inc();
        return;
    }

    // Delegate to worker method
    onAcceptWorker(
        std::move(socket),
        config_->handOffRestoredDestinationConnections(),
        false,  // not rebalanced
        listen_address_->networkNamespace()
    );
}
```

### 3.4 ActiveTcpListener::onAcceptWorker()

**Connection balancing decision:**

```cpp
void ActiveTcpListener::onAcceptWorker(
    Network::ConnectionSocketPtr&& socket,
    bool hand_off_restored_destination_connections,
    bool rebalanced,
    const absl::optional<std::string>& network_namespace)
{
    // Get RTT from socket if available
    absl::optional<std::chrono::milliseconds> t = socket->lastRoundTripTime();
    if (t.has_value()) {
        socket->connectionInfoProvider().setRoundTripTime(t.value());
    }

    // Connection balancing: pick target worker
    if (!rebalanced) {
        Network::BalancedConnectionHandler& target_handler =
            connection_balancer_.pickTargetHandler(*this);

        // If connection should go to different worker, post it
        if (&target_handler != this) {
            target_handler.post(std::move(socket));
            return;
        }
    }

    // Process connection on this worker
    auto active_socket = std::make_unique<ActiveTcpSocket>(
        *this,
        std::move(socket),
        hand_off_restored_destination_connections,
        network_namespace
    );

    // Start listener filter chain (TLS Inspector, Proxy Protocol, etc.)
    active_socket->continueFilterChain(true);

    // Add to list of sockets in listener filter phase
    active_socket->moveIntoListBack(std::move(active_socket), sockets_);
}
```

**From here:**

- `ActiveTcpSocket` runs listener filters
- After listener filters complete, calls `FilterChainManagerImpl::findFilterChain()`
- Matched filter chain used to create network filters
- Connection is established and added to `connections_by_context_` map

---

## 4. Other Method Invocations

### 4.1 onReject()

**Called by:** `TcpListenerImpl::onSocketEvent()` when connection is rejected

**When:**
- Global connection limit reached
- Overload manager signals load shedding
- Random reject fraction triggered

**Code:**

```cpp
void ActiveTcpListener::onReject(RejectCause cause) {
    switch (cause) {
    case RejectCause::GlobalCxLimit:
        stats_.downstream_global_cx_overflow_.inc();
        break;
    case RejectCause::OverloadAction:
        stats_.downstream_cx_overload_reject_.inc();
        break;
    }
}
```

### 4.2 recordConnectionsAcceptedOnSocketEvent()

**Called by:** `TcpListenerImpl::onSocketEvent()` after processing all pending connections

**Purpose:** Record histogram stat of connections accepted per socket event

**Code:**

```cpp
void ActiveTcpListener::recordConnectionsAcceptedOnSocketEvent(
    uint32_t connections_accepted)
{
    stats_.connections_accepted_per_socket_event_.recordValue(connections_accepted);
}
```

### 4.3 pauseListening() / resumeListening()

**Called by:** External request to temporarily pause listener (e.g., during drain)

**Code:**

```cpp
void ActiveTcpListener::pauseListening() {
    if (listener_) {
        listener_->disable();  // Disables file events in event loop
    }
}

void ActiveTcpListener::resumeListening() {
    if (listener_) {
        listener_->enable();  // Re-enables file events
    }
}
```

**Effect:**
- `disable()` calls `socket_->ioHandle().disableFileEvents()` → `epoll_ctl` removes event
- `enable()` calls `socket_->ioHandle().enableFileEvents()` → `epoll_ctl` adds event back

---

## 5. Connection Balancing Path

### 5.1 Connection Posted to Different Worker

**Scenario:** Connection is accepted on worker A but should be processed by worker B

**Steps:**

1. `ActiveTcpListener::onAcceptWorker()` on worker A calls `connection_balancer_.pickTargetHandler(*this)`
2. Balancer returns handler for worker B
3. Worker A calls `target_handler.post(std::move(socket))`
4. **Inside `ActiveTcpListener::post()` on worker A:**
   ```cpp
   void ActiveTcpListener::post(Network::ConnectionSocketPtr&& socket) {
       // Create wrapper for socket
       auto socket_wrapper = std::make_shared<RebalancedSocket>();
       socket_wrapper->socket = std::move(socket);

       // Post to target worker's dispatcher
       tcp_conn_handler_.dispatcher().post([socket_wrapper, this]() {
           if (is_deleting_) {
               return;
           }
           // Now running on worker B's thread
           onAcceptWorker(
               std::move(socket_wrapper->socket),
               config_->handOffRestoredDestinationConnections(),
               true,  // rebalanced = true
               listen_address_->networkNamespace()
           );
       });
   }
   ```
5. Worker B's event loop picks up posted task
6. `ActiveTcpListener::onAcceptWorker()` runs on worker B with `rebalanced=true`
7. Since `rebalanced=true`, skips balancing logic and processes connection locally

**Key insight:** Socket file descriptors can be passed between threads using `sendmsg()` with `SCM_RIGHTS` on Unix, or via abstraction in `Dispatcher::post()`

---

## 6. Config Update Path

### 6.1 Filter Chain Update Without Full Listener Drain

**Scenario:** LDS update changes only filter chains, not listener socket

**Steps:**

1. Main thread: `ListenerManagerImpl` creates new `ListenerImpl` with updated filter chains
2. Main thread: Calls `drainFilterChains(old_listener, new_listener)`
3. Main thread: Marks removed filter chains as draining
4. Main thread: Posts config update to all workers
5. **Worker thread receives update:**
   ```cpp
   // Inside ConnectionHandlerImpl - dispatched from main thread
   listener_detail->invokeListenerMethod([&new_config](
       Network::ConnectionHandler::ActiveListener& listener)
   {
       listener.updateListenerConfig(new_config);
   });
   ```
6. **ActiveTcpListener::updateListenerConfig() invoked:**
   ```cpp
   void ActiveTcpListener::updateListenerConfig(Network::ListenerConfig& config) {
       ENVOY_LOG(trace, "replacing listener {} by {}",
                 config_->listenerTag(),
                 config.listenerTag());

       // Swap config pointer - new connections use new config
       config_ = &config;
   }
   ```
7. **Result:**
   - Existing connections continue using old filter chains
   - New connections use new filter chains from updated config
   - No socket close/reopen, no connection drain

---

## 7. Complete Invocation Timeline

### From Bootstrap to Connection Accept

```
[Bootstrap Config File]
        ↓
[Main Thread: ListenerManagerImpl::addOrUpdateListener()]
        ↓
[Main Thread: ListenerImpl::create()]
        ↓ builds filter factories
[Main Thread: ListenerImpl moves to warming_listeners_]
        ↓
[Main Thread: Warming completes, moves to active_listeners_]
        ↓
[Main Thread: addListenerToWorker(worker, listener)]
        ↓
[Main Thread: worker.addListener() - POSTS TO WORKER THREAD]
        ↓
════════════════════════════════════════════════════════════
        ↓ (crosses thread boundary)
════════════════════════════════════════════════════════════
        ↓
[Worker Thread: Event loop picks up posted addListener task]
        ↓
[Worker Thread: WorkerImpl::addListener()]
        ↓
[Worker Thread: ConnectionHandlerImpl::addListener()]
        ↓
[Worker Thread: new ActiveTcpListener(...)]
        ↓
    [ActiveTcpListener constructor]
            ↓
    [Calls parent.createListener(socket, *this, ...)]
            ↓
    [ConnectionHandlerImpl::createListener()]
            ↓
    [new TcpListenerImpl(dispatcher, socket, cb=ActiveTcpListener)]
            ↓
        [TcpListenerImpl constructor]
                ↓
        [socket->ioHandle().initializeFileEvent(dispatcher, lambda)]
                ↓
        [Event loop: epoll_ctl ADD listen_fd with callback]
            ↓
    [Returns TcpListenerImpl pointer to ActiveTcpListener]
            ↓
    [ActiveTcpListener stores in listener_ member]
        ↓
[Worker Thread: TcpListenerImpl::enable()]
        ↓
[Worker Thread: socket->ioHandle().enableFileEvents(Read)]
        ↓
[Worker Thread: Event loop now monitoring listen socket]

════════════════════════════════════════════════════════════
... TIME PASSES ...
════════════════════════════════════════════════════════════

[Client: Initiates TCP connection]
        ↓
[Kernel: TCP 3-way handshake completes]
        ↓
[Kernel: Connection placed in accept queue]
        ↓
[Kernel: Listen socket becomes readable]
        ↓
[Event Loop: epoll_wait() returns]
        ↓
[Event Loop: Invokes registered callback lambda]
        ↓
[TcpListenerImpl::onSocketEvent(READ)]
        ↓
[TcpListenerImpl: socket->ioHandle().accept() - KERNEL SYSCALL]
        ↓
[TcpListenerImpl: Checks global limit, overload, reject fraction]
        ↓
[TcpListenerImpl: cb_.onAccept(accepted_socket)]
        ↓
════════════════════════════════════════════════════════════
        ↓ (callback crosses abstraction boundary)
════════════════════════════════════════════════════════════
        ↓
[ActiveTcpListener::onAccept(socket)]
        ↓
[ActiveTcpListener: Checks per-listener connection limit]
        ↓
[ActiveTcpListener::onAcceptWorker(socket, rebalanced=false)]
        ↓
[ActiveTcpListener: Connection balancing - pickTargetHandler()]
        ↓
    [IF target is different worker:]
            ↓
        [post(socket) to target worker's dispatcher]
            ↓
        [Target worker's event loop picks up posted socket]
            ↓
        [Target worker's onAcceptWorker(socket, rebalanced=true)]

    [IF target is this worker:]
            ↓
        [new ActiveTcpSocket(*this, socket)]
            ↓
        [ActiveTcpSocket::continueFilterChain()]
            ↓
        [Listener filters run: TLS Inspector, Proxy Protocol, etc.]
            ↓
        [All listener filters complete]
            ↓
        [ActiveTcpSocket::newConnection()]
            ↓
        [FilterChainManagerImpl::findFilterChain(socket, stream_info)]
            ↓
        [Walk matching trie: port → IP → SNI → protocol → ALPN → ...]
            ↓
        [Returns matched FilterChainImpl*]
            ↓
        [ActiveTcpListener::newActiveConnection(filter_chain, socket)]
            ↓
        [TransportSocketFactory::createTransportSocket()]
            ↓
        [new ConnectionImpl(io_handle, transport_socket)]
            ↓
        [For each network filter factory in FilterChainImpl:]
                ↓
            [factory(connection) creates filter instance]
                ↓
            [filter->initializeReadFilter(connection)]
                ↓
            [connection->addReadFilter(filter)]
            ↓
        [new ActiveTcpConnection(connection, listener, filter_chain)]
            ↓
        [Insert into connections_by_context_[filter_chain]]
            ↓
        [INCREMENT stats: downstream_cx_total, downstream_cx_active]
            ↓
════════════════════════════════════════════════════════════

[Connection established and ready to process requests]
```

---

## Key Takeaways

### 1. Callback-Based Invocation

`ActiveTcpListener` is **NOT directly called by user code**. It is invoked via callbacks from:
- `TcpListenerImpl` (when connections arrive)
- Event loop (indirectly via `TcpListenerImpl`)
- Main thread (for config updates via posted tasks)

### 2. One Instance Per Worker Per Listener

Each worker thread gets its own `ActiveTcpListener` instance for each listener. They operate independently and in parallel.

### 3. Event-Driven Architecture

The invocation path is:
```
Kernel → Event Loop → TcpListenerImpl → ActiveTcpListener
```

No polling, no busy waiting - purely event-driven via `epoll`/`kqueue`.

### 4. Thread Safety

`ActiveTcpListener` runs entirely on its worker thread (except construction on main thread). No locks needed for most operations because each instance is thread-local.

### 5. Connection Balancing Cross-Thread

Sockets can be moved between workers using `Dispatcher::post()` and Unix socket passing (`SCM_RIGHTS`), allowing dynamic load balancing.

### 6. Config Updates Without Drain

By updating `config_` pointer, new connections can use new filter chains without disrupting existing connections or requiring socket close/reopen.

---

## Class Interaction Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│ Main Thread                                                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ListenerManagerImpl                                           │
│         │                                                       │
│         │ addListenerToWorker()                                │
│         ▼                                                       │
│  Worker::addListener() ──────────[post to worker thread]────► │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
                                     │
                                     │
                                     ▼
┌─────────────────────────────────────────────────────────────────┐
│ Worker Thread                                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ConnectionHandlerImpl::addListener()                          │
│         │                                                       │
│         │ new ActiveTcpListener(...)                           │
│         ▼                                                       │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ ActiveTcpListener                                    │     │
│  │  implements TcpListenerCallbacks                     │     │
│  │                                                      │     │
│  │  - onAccept(socket)          ◄──────────────┐      │     │
│  │  - onReject(cause)                          │      │     │
│  │  - recordConnectionsAccepted()              │      │     │
│  │  - updateListenerConfig()                   │      │     │
│  └──────────────────────────────────────────────────────┘     │
│         │ createListener(socket, *this)              │        │
│         ▼                                             │        │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ TcpListenerImpl                                    │     │
│  │                                                    │     │
│  │  cb_: TcpListenerCallbacks&                       │     │
│  │       (points to ActiveTcpListener)               │     │
│  │                                                    │     │
│  │  - onSocketEvent(READ) ─────────────────────┐    │     │
│  │       │ accept()                             │    │     │
│  │       │ cb_.onAccept(socket) ────────────────┼────┘     │
│  │       │ cb_.onReject(cause)                  │          │
│  └──────────────────────────────────────────────────────┘     │
│         │ initializeFileEvent(dispatcher, lambda)            │
│         ▼                                                     │
│  ┌──────────────────────────────────────────────────────┐     │
│  │ Event::Dispatcher                                   │     │
│  │                                                     │     │
│  │  - registerFileEvent(fd, callback)                │     │
│  │  - epoll_wait() / kevent()                        │     │
│  │  - on READ event: invoke callback ────────────────┘     │
│  └──────────────────────────────────────────────────────┘     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Related Documentation

- [CODE_PATH_SCENARIOS.md](CODE_PATH_SCENARIOS.md) - Detailed code paths for all scenarios
- [OVERVIEW_PART3_active_tcp.md](OVERVIEW_PART3_active_tcp.md) - ActiveTcpListener and ActiveTcpSocket architecture
- [connection_handler_impl.md](connection_handler_impl.md) - ConnectionHandlerImpl details
- [active_tcp_listener_and_socket.md](active_tcp_listener_and_socket.md) - Complete ActiveTcpListener documentation
