# Listener Manager — Terminology Guide

**Purpose:** Clear mapping of common terms to actual class names and their relationships

---

## Table of Contents

1. [Filter Terminology](#1-filter-terminology)
2. [Filter Chain Terminology](#2-filter-chain-terminology)
3. [Factory Terminology](#3-factory-terminology)
4. [Socket Terminology](#4-socket-terminology)
5. [Listener Terminology](#5-listener-terminology)
6. [Connection Terminology](#6-connection-terminology)
7. [Active vs Implementation Classes](#7-active-vs-implementation-classes)
8. [Context Terminology](#8-context-terminology)
9. [Common Confusions Clarified](#9-common-confusions-clarified)

---

## 1. Filter Terminology

### 1.1 Three Types of Filters

Envoy has **three distinct types of filters** that operate at different layers. They are NOT interchangeable.

#### Listener Filters

**Interface:** `Network::ListenerFilter` in `envoy/network/filter.h`

**When they run:** BEFORE connection is established, during accept phase

**Purpose:** Inspect initial connection data to populate socket metadata (SNI, ALPN, original destination, etc.)

**Examples:**
- `TLS Inspector` - peeks at TLS ClientHello to extract SNI and ALPN
- `Proxy Protocol` - reads PROXY protocol header for real client IP
- `Original Destination` - retrieves original destination IP via `SO_ORIGINAL_DST`
- `HTTP Inspector` - detects HTTP protocol

**Key methods:**
```cpp
class ListenerFilter {
    virtual FilterStatus onAccept(ListenerFilterCallbacks& cb) PURE;
    virtual FilterStatus onData(Buffer::Instance& data) PURE;  // optional
};
```

**Lifecycle:** Created per accepted socket, destroyed after filter chain matching completes

**Does NOT have access to:** Connection object (it doesn't exist yet)

#### Network Filters

**Interface:** `Network::ReadFilter` and `Network::WriteFilter` in `envoy/network/filter.h`

**When they run:** AFTER connection is established, for entire connection lifetime

**Purpose:** Process binary data on the connection (TCP/TLS level)

**Examples:**
- `TCP Proxy` - proxies raw TCP connections
- `HTTP Connection Manager` - parses HTTP protocol, manages HTTP filter chain
- `Redis Proxy` - parses Redis protocol
- `Mongo Proxy` - parses MongoDB protocol
- `MySQL Proxy` - parses MySQL protocol

**Key methods:**
```cpp
class ReadFilter {
    virtual FilterStatus onNewConnection() PURE;
    virtual FilterStatus onData(Buffer::Instance& data, bool end_stream) PURE;
    virtual void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) PURE;
};

class WriteFilter {
    virtual FilterStatus onWrite(Buffer::Instance& data, bool end_stream) PURE;
    virtual void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) PURE;
};
```

**Lifecycle:** Created per connection, destroyed when connection closes

**Has access to:** `Network::Connection` object

**Important:** HTTP Connection Manager is a **network filter** that internally manages HTTP filters

#### HTTP Filters

**Interface:** `Http::StreamDecoderFilter`, `Http::StreamEncoderFilter`, `Http::StreamFilter` in `envoy/http/filter.h`

**When they run:** AFTER HTTP protocol is parsed by HTTP Connection Manager

**Purpose:** Process HTTP requests and responses (headers, body, trailers)

**Examples:**
- `Router` - selects upstream cluster and forwards request
- `Fault Injection` - injects delays and errors
- `CORS` - handles cross-origin resource sharing
- `Rate Limit` - enforces rate limits
- `JWT Authentication` - validates JWT tokens
- `RBAC` - role-based access control
- `Gzip` - compresses responses

**Key methods:**
```cpp
class StreamDecoderFilter {  // Request path
    virtual FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) PURE;
    virtual FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) PURE;
    virtual FilterTrailersStatus decodeTrailers(RequestTrailerMap& trailers) PURE;
};

class StreamEncoderFilter {  // Response path
    virtual FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) PURE;
    virtual FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) PURE;
    virtual FilterTrailersStatus encodeTrailers(ResponseTrailerMap& trailers) PURE;
};
```

**Lifecycle:** Created per HTTP request/response stream, destroyed when stream completes

**Has access to:** HTTP-level constructs (headers, metadata), NOT raw TCP data

---

## 2. Filter Chain Terminology

### 2.1 Network Filter Chain

**What it is:** An ordered list of **network filters** that process data on a connection

**Class:** `Network::FilterChain` in `envoy/network/filter.h`

**Implementation:** `FilterChainImpl` in `source/common/listener_manager/filter_chain_manager_impl.h`

**What it contains:**
```cpp
class FilterChain {
    virtual const DownstreamTransportSocketFactory& transportSocketFactory() const PURE;
    virtual const NetworkFilterFactoriesList& networkFilterFactories() const PURE;
    virtual absl::string_view name() const PURE;
    virtual bool addedViaApi() const PURE;
};
```

**Key members in `FilterChainImpl`:**
- `transport_socket_factory_: DownstreamTransportSocketFactoryPtr` - creates TLS or raw transport socket
- `filters_factory_: NetworkFilterFactoriesList` - list of network filter factory callbacks
- `transport_socket_connect_timeout_: std::chrono::milliseconds`
- `filter_chain_info_: FilterChainInfoSharedPtr`

**Important:** `Network::FilterChain` contains **only network filters**, NOT HTTP filters

### 2.2 HTTP Filter Chain

**What it is:** An ordered list of **HTTP filters** managed by HTTP Connection Manager

**NOT a class named "HttpFilterChain"** - it's managed internally by `Http::FilterManager`

**Where it's configured:** Inside the `http_connection_manager` network filter config

**How it's built:** `Http::FilterChainFactoryCallbacks` creates HTTP filters from config

**Example config:**
```yaml
filter_chains:
- filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      http_filters:  # This is the HTTP filter chain
      - name: envoy.filters.http.fault
      - name: envoy.filters.http.cors
      - name: envoy.filters.http.router
```

**Key point:** HTTP filter chain is a **separate concept** nested inside the network filter chain

### 2.3 Listener Filter Chain

**What it is:** An ordered list of **listener filters** that run before connection establishment

**NOT a standalone class** - managed by `ListenerFilterManager` interface

**Implementation:** Listener filters are stored in `ListenerImpl::listener_filter_factories_`

**Where it's configured:** In the listener's `listener_filters` field

**Example config:**
```yaml
listeners:
- name: my_listener
  listener_filters:  # This is the listener filter chain
  - name: envoy.filters.listener.tls_inspector
  - name: envoy.filters.listener.proxy_protocol
  filter_chains:  # These are network filter chains
  - filters: [...]
```

### 2.4 Drainable Filter Chain

**Interface:** `Network::DrainableFilterChain` in `envoy/network/filter.h`

**Extends:** `Network::FilterChain`

**Additional method:**
```cpp
class DrainableFilterChain : public FilterChain {
    virtual void startDraining() PURE;
};
```

**Purpose:** Allows filter chain to be marked as draining during config updates

**Implementation:** `FilterChainImpl` in `filter_chain_manager_impl.h` implements this interface

---

## 3. Factory Terminology

### 3.1 Filter Factories vs Factory Callbacks

**Critical distinction:** "Factory" can mean two things:

#### Config Factory (Extension Factory)

**Purpose:** Creates filter factory callbacks from protobuf config

**Interface:** `Server::Configuration::NamedNetworkFilterConfigFactory`

**When it runs:** At config load time (once)

**What it does:** Parses proto config, validates it, returns a factory callback

**Example:** `HttpConnectionManagerFilterConfigFactory`

**Key method:**
```cpp
class NamedNetworkFilterConfigFactory {
    virtual FilterFactoryCb createFilterFactoryFromProto(
        const Protobuf::Message& config,
        FactoryContext& context) PURE;
};
```

**Returns:** Factory callback (see below)

#### Factory Callback

**Purpose:** Creates actual filter instances per connection/request

**Type:** `std::function` that captures config and shared resources

**When it runs:** At connection establishment time (per connection) or request time (per HTTP stream)

**What it does:** Instantiates filter and registers it with filter manager

**Types:**

```cpp
// Network filter factory callback
using FilterFactoryCb = std::function<void(FilterManager& filter_manager)>;

// Listener filter factory callback
using ListenerFilterFactoryCb = std::function<void(ListenerFilterManager& filter_manager)>;

// HTTP filter factory callback (in HTTP Connection Manager)
using FilterFactoryCb = std::function<void(Http::FilterChainFactoryCallbacks& callbacks)>;
```

**Example factory callback:**
```cpp
FilterFactoryCb callback = [config](FilterManager& manager) {
    auto filter = std::make_unique<TcpProxyFilter>(config);
    manager.addReadFilter(filter);
};
```

**Stored in:** `FilterChainImpl::filters_factory_` (for network filters)

### 3.2 Socket Factories

#### ListenSocketFactory

**Interface:** `Network::ListenSocketFactory` in `envoy/network/listener.h`

**Implementation:** `ListenSocketFactoryImpl` in `listener_impl.h`

**Purpose:** Creates listen sockets for a listener

**When created:** During listener configuration

**Key methods:**
```cpp
class ListenSocketFactory {
    virtual Network::SocketSharedPtr getListenSocket(uint32_t worker_index) PURE;
    virtual Network::Socket::Type socketType() const PURE;
    virtual const Network::Address::InstanceConstSharedPtr& localAddress() const PURE;
};
```

**Stored in:** `ListenerImpl` has `listenSocketFactories()` (usually 1, can be multiple)

**What it holds:** Pre-created sockets (one per worker for `SO_REUSEPORT`, or one shared socket)

#### TransportSocketFactory

**Interfaces:**
- `Network::DownstreamTransportSocketFactory` (for server connections)
- `Network::UpstreamTransportSocketFactory` (for client connections)

**Implementations:**
- `RawBufferSocketFactory` - creates plain TCP sockets
- `ServerSslSocketFactory` - creates TLS server sockets
- `ClientSslSocketFactory` - creates TLS client sockets

**Purpose:** Creates transport sockets (TLS or raw) for individual connections

**When created:** During filter chain configuration

**Key method:**
```cpp
class DownstreamTransportSocketFactory {
    virtual TransportSocketPtr createDownstreamTransportSocket() const PURE;
    virtual bool implementsSecureTransport() const PURE;
};
```

**Stored in:** `FilterChainImpl::transport_socket_factory_`

**Per connection:** `TransportSocketFactory::createTransportSocket()` is called to create socket for that connection

### 3.3 Filter Chain Factory Builder

**Interface:** `FilterChainFactoryBuilder` in `filter_chain_manager_impl.h`

**Implementation:** `ListenerFilterChainFactoryBuilder` in `listener_manager_impl.h`

**Purpose:** Builds complete `FilterChainImpl` from protobuf config

**Key method:**
```cpp
class FilterChainFactoryBuilder {
    virtual absl::StatusOr<Network::DrainableFilterChainSharedPtr>
    buildFilterChain(
        const envoy::config::listener::v3::FilterChain& filter_chain,
        FilterChainFactoryContextCreator& context_creator,
        bool added_via_api) const PURE;
};
```

**Called by:** `FilterChainManagerImpl::addFilterChains()`

**What it does:**
1. Creates filter chain factory context
2. Calls `createNetworkFilterFactoryList()` to build network filter factories
3. Calls `createTransportSocketFactory()` to build transport socket factory
4. Wraps everything in `FilterChainImpl`

---

## 4. Socket Terminology

### 4.1 Socket Hierarchy

```
Network::Socket (abstract base)
    ├── Network::ConnectionSocket (connected socket - client or accepted)
    │       └── ConnectionSocketImpl (implementation)
    │               └── AcceptedSocketImpl (socket from accept())
    └── Network::ListenSocket (listening socket)
            └── ListenSocketImpl (implementation)
```

### 4.2 Socket Types

#### Listen Socket

**Interface:** `Network::ListenSocket` (no separate interface, just `Network::Socket`)

**Implementation:** `ListenSocketImpl` in `source/common/network/listen_socket_impl.h`

**Purpose:** Socket in `LISTEN` state, bound to address, waiting for connections

**Created by:** `ListenSocketFactory::getListenSocket()`

**File descriptor:** Result of `socket()` + `bind()` + `listen()` syscalls

**Owned by:** `TcpListenerImpl` (wrapped in `SocketSharedPtr`)

**Never used for:** Sending or receiving data - only for accepting connections

#### Connection Socket

**Interface:** `Network::ConnectionSocket` in `envoy/network/listen_socket.h`

**Purpose:** Socket for an established connection (either client-initiated or server-accepted)

**Key methods:**
```cpp
class ConnectionSocket : public virtual Socket {
    virtual Connection* connection() const PURE;
    virtual void setDetectedTransportProtocol(absl::string_view protocol) PURE;
    virtual void setRequestedServerName(const absl::string_view server_name) PURE;
    virtual void setRequestedApplicationProtocols(const std::vector<absl::string_view>&) PURE;
};
```

**Metadata populated by:** Listener filters (TLS Inspector, Proxy Protocol, etc.)

**Used for:** Filter chain matching

#### Accepted Socket

**Implementation:** `AcceptedSocketImpl` in `source/common/network/connection_socket_impl.h`

**Extends:** `ConnectionSocketImpl` (which implements `ConnectionSocket`)

**Purpose:** Socket returned from `accept()` syscall

**Created by:** `TcpListenerImpl::onSocketEvent()` after calling `socket->accept()`

**File descriptor:** Result of `accept4()` syscall

**Additional responsibilities:**
- Tracks global connection count for limit enforcement
- Releases global connection resource on destruction

### 4.3 Transport Socket

**Interface:** `Network::TransportSocket` in `envoy/network/transport_socket.h`

**Purpose:** Abstraction layer for transport encryption (TLS vs raw TCP)

**Implementations:**
- `RawBufferSocket` - plain TCP, no encryption
- `SslSocket` - TLS encryption using BoringSSL
- `AltsSocket` - ALTS (Application Layer Transport Security) for gRPC

**Created by:** `TransportSocketFactory::createTransportSocket()`

**Wraps:** IO handle (file descriptor)

**Key methods:**
```cpp
class TransportSocket {
    virtual IoResult doRead(Buffer::Instance& buffer) PURE;
    virtual IoResult doWrite(Buffer::Instance& buffer, bool end_stream) PURE;
    virtual void onConnected() PURE;
};
```

**Owned by:** `Network::ConnectionImpl`

**Relationship:** `ConnectionImpl` → owns → `TransportSocket` → wraps → `IoHandle` (file descriptor)

---

## 5. Listener Terminology

### 5.1 Listener Class Hierarchy

```
ListenerConfig (interface - config access)
    └── ListenerImpl (config representation on main thread)

Network::Listener (interface - event handling)
    ├── TcpListenerImpl (TCP listener using epoll/kqueue)
    ├── UdpListenerImpl (UDP listener)
    └── QuicListenerImpl (QUIC listener)

Network::ConnectionHandler::ActiveListener (interface - lifecycle)
    ├── ActiveTcpListener (worker thread TCP listener wrapper)
    ├── ActiveUdpListener (worker thread UDP listener wrapper)
    └── ActiveInternalListener (internal listeners)
```

### 5.2 Listener Types

#### ListenerImpl

**File:** `source/common/listener_manager/listener_impl.h`

**Purpose:** Represents listener configuration on main thread

**Lifecycle:** Created by `ListenerManagerImpl`, exists on main thread only

**What it contains:**
- `FilterChainManagerImpl` - manages filter chain matching
- `listener_filter_factories_` - list of listener filter factory callbacks
- `ListenSocketFactoryImpl` - creates listen sockets
- `PerListenerFactoryContextImpl` - factory context for this listener

**Key methods:**
```cpp
class ListenerImpl : public Network::ListenerConfig, public Network::FilterChainFactory {
    const Network::Address::InstanceConstSharedPtr& address() const;
    const std::vector<ListenSocketFactoryPtr>& listenSocketFactories() const;
    FilterChainManager& filterChainManager();
    bool createListenerFilterChain(ListenerFilterManager&);
};
```

**Does NOT:** Actually accept connections (that's done by `TcpListenerImpl` on worker threads)

#### TcpListenerImpl

**File:** `source/common/network/tcp_listener_impl.h`

**Purpose:** OS-level TCP listener, handles actual `accept()` calls

**Lifecycle:** Created per worker thread by `ConnectionHandlerImpl`

**What it does:**
- Registers listen socket with event loop (`epoll_ctl` / `kevent`)
- Receives file descriptor read events when connections are pending
- Calls `accept()` to get new connections from kernel
- Invokes `TcpListenerCallbacks::onAccept()` for accepted sockets

**Key members:**
- `cb_: TcpListenerCallbacks&` - reference to `ActiveTcpListener`
- `socket_: SocketSharedPtr` - the listen socket
- File event registered with dispatcher

**Does NOT:** Process connections - just accepts them and passes to callback

#### ActiveTcpListener

**File:** `source/common/listener_manager/active_tcp_listener.h`

**Purpose:** Worker thread wrapper for TCP listener, manages connection lifecycle

**Lifecycle:** One instance per listener per worker thread

**Implements:** `Network::TcpListenerCallbacks` (receives accept events from `TcpListenerImpl`)

**What it contains:**
- `listener_: Network::ListenerPtr` - holds `TcpListenerImpl`
- `config_: Network::ListenerConfig*` - pointer to listener config
- `sockets_: std::list<ActiveTcpSocket>` - sockets in listener filter phase
- `connections_by_context_: map<FilterChain*, ActiveConnections>` - established connections grouped by filter chain

**Key methods:**
```cpp
class ActiveTcpListener : public Network::TcpListenerCallbacks {
    void onAccept(Network::ConnectionSocketPtr&& socket) override;
    void newActiveConnection(const Network::FilterChain&, Network::ServerConnectionPtr);
    void updateListenerConfig(Network::ListenerConfig& config) override;
};
```

**Bridge:** Connects `TcpListenerImpl` (OS listener) to `ActiveTcpSocket` (listener filters) to `ConnectionImpl` (network filters)

---

## 6. Connection Terminology

### 6.1 Connection Class Hierarchy

```
Network::Connection (interface)
    └── Network::ConnectionImpl (implementation)
            └── Network::ServerConnectionImpl (accepted connections)
            └── Network::ClientConnectionImpl (client-initiated connections)

Network::FilterManager (interface - filter chain management)
    └── Network::FilterManagerImpl (implementation in ConnectionImpl)
```

### 6.2 Connection Types

#### ConnectionImpl

**File:** `source/common/network/connection_impl.h`

**Purpose:** Represents an established TCP connection with network filter chain

**What it contains:**
- `transport_socket_: TransportSocketPtr` - handles encryption/decryption
- `io_handle_: IoHandlePtr` - file descriptor wrapper
- `filter_manager_: FilterManagerImpl` - manages network filter chain
- `read_buffer_: Buffer::Instance` - incoming data buffer
- `write_buffer_: Buffer::Instance` - outgoing data buffer

**Key methods:**
```cpp
class ConnectionImpl : public Network::Connection {
    void addReadFilter(ReadFilterSharedPtr filter);
    void addWriteFilter(WriteFilterSharedPtr filter);
    void write(Buffer::Instance& data, bool end_stream);
    void close(ConnectionCloseType type);
};
```

**Event flow:**
1. Event loop signals socket is readable
2. `transport_socket_->doRead()` reads and decrypts data into `read_buffer_`
3. `filter_manager_.onRead()` passes data through network filters
4. Filters process data (e.g., HTTP Connection Manager parses HTTP)

#### ServerConnectionImpl

**What it is:** `ConnectionImpl` for server-side (accepted) connections

**Created by:** `ActiveTcpListener::newActiveConnection()`

**Differences from client:** May have different timeout behavior, transport socket settings

#### ClientConnectionImpl

**What it is:** `ConnectionImpl` for client-side (upstream) connections

**Created by:** Cluster connection pools when connecting to upstreams

**Differences from server:** Initiates connection, different SSL context (client vs server)

---

## 7. Active vs Implementation Classes

### 7.1 "Active" Terminology

**Pattern:** "Active" classes are **worker-thread wrappers** that manage lifecycle and state

**Not in the name:** The classes don't inherit from a base class called "Active" - it's just a naming convention

### 7.2 Active Classes

#### ActiveTcpListener

**Purpose:** Worker thread wrapper for TCP listener

**Manages:**
- OS listener (`TcpListenerImpl`)
- Sockets in listener filter phase (`ActiveTcpSocket`)
- Established connections (`ActiveTcpConnection`)
- Connection balancing across workers

**One per:** Listener per worker thread

#### ActiveTcpSocket

**File:** `source/common/listener_manager/active_tcp_socket.h`

**Purpose:** Wrapper for accepted socket during listener filter phase

**Manages:**
- Accepted socket
- Listener filter chain execution
- Listener filter timeout
- Transition to connection establishment

**Lifecycle:** Short-lived (milliseconds) - from accept to filter chain matching

**Stored in:** `ActiveTcpListener::sockets_` list

**Methods:**
```cpp
class ActiveTcpSocket {
    void startFilterChain();
    void continueFilterChain(bool success);
    void newConnection();  // After listener filters complete
};
```

#### ActiveTcpConnection

**File:** `source/common/listener_manager/active_stream_listener_base.h`

**Purpose:** Wrapper linking `ConnectionImpl` to `ActiveTcpListener`

**What it contains:**
- `connection_: Network::ConnectionPtr` - the actual connection
- `listener_: ActiveTcpListener&` - reference to owning listener
- `filter_chain_: const Network::FilterChain*` - which filter chain is used

**Lifecycle:** Entire duration of connection

**Stored in:** `ActiveTcpListener::connections_by_context_[filter_chain]`

**Why it exists:**
- Tracks which filter chain the connection is using
- Allows listener to notify connection when filter chain is drained
- Provides backpointer from connection to listener for stats

### 7.3 Implementation Classes

**Pattern:** "Impl" suffix indicates actual implementation of an interface

#### ConnectionImpl

Implements `Network::Connection` interface

#### FilterChainManagerImpl

Implements `Network::FilterChainManager` interface

#### ListenerManagerImpl

Implements `Server::ListenerManager` interface

#### TcpListenerImpl

Implements `Network::Listener` interface (no "Active" because it's pure I/O, not lifecycle)

---

## 8. Context Terminology

### 8.1 Context Hierarchy

Factory contexts provide access to Envoy internals (stats, clusters, runtime, etc.) for configuration and filter creation.

```
ServerFactoryContext (top level - server-wide)
    └── ListenerFactoryContextBaseImpl (per listener)
            └── PerListenerFactoryContextImpl (per listener with stats scope)
                    └── PerFilterChainFactoryContextImpl (per filter chain)
```

### 8.2 Context Types

#### ServerFactoryContext

**Interface:** `Server::Configuration::ServerFactoryContext`

**Purpose:** Provides access to server-wide resources

**Available:**
- `clusterManager()` - access to upstream clusters
- `admin()` - admin interface
- `runtime()` - runtime feature flags
- `stats()` - stats store
- `api()` - API for file I/O, threading, etc.

**Scope:** Entire Envoy process

#### ListenerFactoryContextBaseImpl

**File:** `listener_impl.h`

**Purpose:** Base context shared by all filter chains in a listener

**Additional access:**
- `drainDecision()` - whether listener is draining
- `drainManager()` - manages drain lifecycle
- `initManager()` - async initialization

**Scope:** One listener

#### PerListenerFactoryContextImpl

**Extends:** ListenerFactoryContextBaseImpl

**Additional:**
- `listenerScope()` - stats scope prefixed with listener name
- `listenerInfo()` - listener metadata

**Used by:** Listener filter factories

#### PerFilterChainFactoryContextImpl

**File:** `filter_chain_manager_impl.h`

**Purpose:** Context specific to one filter chain

**Additional:**
- `startDraining()` - marks this filter chain as draining
- Filter chain-specific stats scope

**Used by:** Network filter factories

**Lifecycle:** Lives as long as filter chain exists (even after marked draining)

---

## 9. Common Confusions Clarified

### 9.1 Does Network::FilterChain Include HTTP Filters?

**Answer: NO**

`Network::FilterChain` contains **only network filters** (TCP level).

HTTP filters are **nested inside** the HTTP Connection Manager network filter.

**Example:**
```
Network::FilterChain
    └── network_filters_:
            └── HttpConnectionManager (this is a network filter)
                    └── http_filter_chain_:
                            ├── FaultFilter (HTTP filter)
                            ├── CorsFilter (HTTP filter)
                            └── RouterFilter (HTTP filter)
```

**Class relationships:**
- `Network::FilterChain` → has → `NetworkFilterFactoriesList`
- `HttpConnectionManager` (network filter) → has → `Http::FilterChainManager`
- `Http::FilterChainManager` → has → HTTP filter factories

### 9.2 FilterChainImpl vs FilterChainManagerImpl

**`FilterChainImpl`:** Represents ONE filter chain (transport socket + network filters)

**`FilterChainManagerImpl`:** Manages MANY filter chains, performs matching to select one

**Relationship:**
```cpp
class FilterChainManagerImpl {
    FcContextMap fc_contexts_;  // map of proto config → FilterChainImpl
    Network::DrainableFilterChainSharedPtr default_filter_chain_;

    const FilterChain* findFilterChain(socket, info);  // Returns ONE FilterChainImpl
};
```

### 9.3 Filter vs Filter Factory vs Factory Callback

**Filter:** Instance that processes data (created per connection or per request)
- Example: `HttpConnectionManager` instance

**Filter Factory:** Extension that creates filter factory callbacks from config (singleton, registered in registry)
- Example: `HttpConnectionManagerFilterConfigFactory`

**Factory Callback:** Closure that creates filter instances (created once per filter chain, called per connection)
- Example: `[config](FilterManager& mgr) { mgr.addReadFilter(std::make_unique<HCM>(config)); }`

**Timeline:**
1. **Config time:** Filter Factory parses config → returns Factory Callback
2. **Config time:** Factory Callback stored in `FilterChainImpl`
3. **Connection time:** Factory Callback invoked → creates Filter instance
4. **Connection time:** Filter instance added to connection's filter manager

### 9.4 Listen Socket vs Connection Socket

**Listen Socket:**
- State: `LISTEN`
- Created: Once per listener (or once per worker with `SO_REUSEPORT`)
- Purpose: Accept new connections
- Never transfers data

**Connection Socket:**
- State: `ESTABLISHED`
- Created: Per connection (result of `accept()`)
- Purpose: Send/receive data for one connection
- Wrapped by `TransportSocket` for encryption

**File descriptor lifecycle:**
```
socket() → bind() → listen() = Listen Socket (fd 10)
    ↓
accept() → new fd = Connection Socket (fd 42)
    ↓
read(42) / write(42) - actual data transfer
```

### 9.5 ActiveTcpListener vs TcpListenerImpl

**`TcpListenerImpl`:**
- Thin wrapper around OS listen socket
- Calls `accept()` when socket is readable
- Purely I/O focused

**`ActiveTcpListener`:**
- Worker thread lifecycle manager
- Receives accepted sockets from `TcpListenerImpl`
- Runs listener filters
- Matches filter chains
- Creates connections
- Tracks connection stats

**Relationship:**
```
ActiveTcpListener
    └── listener_: ListenerPtr
            └── TcpListenerImpl
                    └── cb_: TcpListenerCallbacks& (points back to ActiveTcpListener)
```

### 9.6 NetworkFilterFactoriesList - What Is It?

**Type:**
```cpp
using NetworkFilterFactoriesList =
    std::vector<FilterConfigProviderPtr<FilterFactoryCb>>;
```

**Breaking it down:**
- `FilterFactoryCb` = `std::function<void(FilterManager&)>` - creates one filter
- `FilterConfigProvider` = wrapper that can be static or dynamic (for filter warming)
- `NetworkFilterFactoriesList` = vector of these providers

**Stored in:** `FilterChainImpl::filters_factory_`

**Usage:** When connection is created, iterate through list and call each factory callback to create filters

**Dynamic vs static:**
- **Static:** Filter config is in listener config, factory callback is ready immediately
- **Dynamic:** Filter config fetched from ECDS, may need warming before use

### 9.7 What Gets Created When?

**Config time (main thread):**
- ✅ `ListenerImpl`
- ✅ `FilterChainManagerImpl`
- ✅ `FilterChainImpl` instances
- ✅ Network filter factory callbacks
- ✅ Listener filter factory callbacks
- ✅ Transport socket factories
- ❌ Filter instances
- ❌ Connections

**Worker startup (worker thread):**
- ✅ `ActiveTcpListener`
- ✅ `TcpListenerImpl`
- ❌ Filter instances
- ❌ Connections

**Connection accept (worker thread):**
- ✅ `ActiveTcpSocket`
- ✅ Listener filter instances
- ❌ Connection
- ❌ Network filter instances

**After listener filters complete (worker thread):**
- ✅ `ConnectionImpl`
- ✅ `TransportSocket`
- ✅ Network filter instances
- ✅ `ActiveTcpConnection`

**HTTP request arrives (worker thread):**
- ✅ HTTP filter instances (created by HTTP Connection Manager)

---

## Quick Reference Table

| Term | Interface/Type | Implementation | Purpose |
|------|---------------|----------------|---------|
| **Listener filter** | `Network::ListenerFilter` | TLS Inspector, Proxy Protocol | Inspect socket before connection |
| **Network filter** | `Network::ReadFilter`, `Network::WriteFilter` | TCP Proxy, HCM, Redis Proxy | Process connection data |
| **HTTP filter** | `Http::StreamDecoderFilter`, `Http::StreamEncoderFilter` | Router, CORS, Fault | Process HTTP requests |
| **Filter chain** | `Network::FilterChain` | `FilterChainImpl` | Transport socket + network filters for one chain |
| **Filter chain manager** | `Network::FilterChainManager` | `FilterChainManagerImpl` | Matches connection to filter chain |
| **Network filter factory callback** | `FilterFactoryCb` | Lambda closure | Creates network filter instance |
| **Listener filter factory callback** | `ListenerFilterFactoryCb` | Lambda closure | Creates listener filter instance |
| **Listen socket** | `Network::Socket` | `ListenSocketImpl` | OS socket in LISTEN state |
| **Connection socket** | `Network::ConnectionSocket` | `ConnectionSocketImpl`, `AcceptedSocketImpl` | OS socket in ESTABLISHED state |
| **Transport socket** | `Network::TransportSocket` | `SslSocket`, `RawBufferSocket` | Encryption layer wrapping IO handle |
| **Listener (config)** | `Network::ListenerConfig` | `ListenerImpl` | Listener configuration (main thread) |
| **Listener (I/O)** | `Network::Listener` | `TcpListenerImpl` | OS listener, accepts connections |
| **Active listener** | `ActiveListener` | `ActiveTcpListener` | Worker thread wrapper, manages lifecycle |
| **Connection** | `Network::Connection` | `ConnectionImpl` | Established connection with filters |
| **Active socket** | N/A | `ActiveTcpSocket` | Socket during listener filter phase |
| **Active connection** | N/A | `ActiveTcpConnection` | Links connection to listener |
| **Transport socket factory** | `DownstreamTransportSocketFactory` | `ServerSslSocketFactory`, `RawBufferSocketFactory` | Creates transport sockets |
| **Listen socket factory** | `Network::ListenSocketFactory` | `ListenSocketFactoryImpl` | Creates listen sockets |
| **Filter chain factory builder** | `FilterChainFactoryBuilder` | `ListenerFilterChainFactoryBuilder` | Builds FilterChainImpl from proto |

---

## Related Documentation

- [CODE_PATH_SCENARIOS.md](CODE_PATH_SCENARIOS.md) - Code paths for config/connection/request time
- [ACTIVE_TCP_LISTENER_INVOCATION.md](ACTIVE_TCP_LISTENER_INVOCATION.md) - How ActiveTcpListener is invoked
- [OVERVIEW_PART2_filter_chains.md](OVERVIEW_PART2_filter_chains.md) - Filter chain architecture
- [BASE_CLASSES_AND_INTERFACES.md](BASE_CLASSES_AND_INTERFACES.md) - Interface definitions
