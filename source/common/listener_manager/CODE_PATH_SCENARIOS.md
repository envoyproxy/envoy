# Listener Manager — Code Path Scenarios

**Directory:** `source/common/listener_manager/`
**Purpose:** Plain-text descriptions of code paths through listener manager for various scenarios

---

## Table of Contents

1. [Config Time Scenarios](#1-config-time-scenarios)
2. [Connection Time Scenarios](#2-connection-time-scenarios)
3. [Request Time Scenarios](#3-request-time-scenarios)
4. [Filter Factory Creation](#4-filter-factory-creation)
5. [Filter Instance Creation](#5-filter-instance-creation)
6. [Draining Process During Filter Chain Updates](#6-draining-process-during-filter-chain-updates)

---

## 1. Config Time Scenarios

### 1.1 Initial Listener Configuration (Bootstrap)

**When:** Envoy starts up and reads static listeners from bootstrap config

**Code Path:**

- `ListenerManagerImpl::addOrUpdateListener()` is called with listener proto from bootstrap
- Method checks if listener with same name already exists in `active_listeners_` map
- Since it's new, creates `ListenerImpl` via `ListenerImpl::create()`
- **Inside `ListenerImpl` constructor:**
  - Creates `ListenerFactoryContextBaseImpl` to provide factory context for this listener
  - Creates `PerListenerFactoryContextImpl` wrapping the base context
  - Creates `Init::ManagerImpl` to track async initialization targets
  - Calls `buildFilterChains()` to process all filter chains from config
- **Inside `buildFilterChains()`:**
  - Creates `FilterChainManagerImpl` to manage filter chain matching and storage
  - Loops through each `filter_chain` entry in listener config proto
  - For each filter chain, calls `FilterChainManagerImpl::addFilterChains()`
  - Also processes `default_filter_chain` if configured
- **Inside `FilterChainManagerImpl::addFilterChains()`:**
  - For each filter chain proto, creates factory context via `createFilterChainFactoryContext()`
  - Calls `ListenerFilterChainFactoryBuilder::buildFilterChain()` to build the chain
  - Inserts built `FilterChainImpl` into nested matching trie structure (`FilterChainsByMatcher`)
  - The trie is organized by: dst_port → dst_ip → sni → transport_proto → alpn → direct_src_ip → src_type → src_ip → src_port
- **Filter factory creation happens at this time** (see section 4)
- Listener moves to `warming_listeners_` map
- `ListenerImpl::initialize()` is called to complete async initialization
- Once warmed, listener moves to `active_listeners_` map
- `addListenerToWorker()` sends listener to all worker threads via `Worker::addListener()`
- Each worker creates `ActiveTcpListener` instance to handle accepts on that worker

### 1.2 Listener Configuration via LDS (Listener Discovery Service)

**When:** Control plane sends listener config update via xDS

**Why LDS Updates Matter:**
- Enables zero-downtime configuration changes
- Supports gradual rollouts and A/B testing
- Allows certificate rotation without restart
- Enables dynamic filter chain updates

**Update Decision Logic:**
The code makes intelligent decisions about how to apply updates:
- **No-op**: Config hash unchanged → skip update entirely
- **In-place filter chain update**: Socket config unchanged → update filter chains without draining connections
- **Full listener update**: Socket config changed → drain old listener, create new one

**Code Path:**

- `LdsApiImpl::onConfigUpdate()` receives listener proto from xDS server
- Validates listener proto using `MessageUtil::validate()`
- Calls `ListenerManagerImpl::addOrUpdateListener()` with new listener config
- **If listener with same name exists:**
  - Computes hash of new config and compares with existing listener's hash
  - If hashes match, listener is unchanged → no-op, return early
  - If hashes differ, checks if it's a filter-chain-only update via `ListenerMessageUtil::filterChainOnlyChange()`
  - **If filter-chain-only update AND listener supports it:**
    - Calls `existing_listener->supportUpdateFilterChain()` to validate compatibility
    - Creates new `ListenerImpl` via `existing_listener->newListenerWithFilterChain()`
    - This reuses socket factories and listener filter chains, only rebuilds network filter chains
    - Calls `drainFilterChains()` to gracefully drain old filter chains (see section 6)
  - **If full listener update needed:**
    - Creates entirely new `ListenerImpl` from scratch
    - Old listener moves to `draining_listeners_` list
    - Old listener's `startDraining()` is called
    - Drain timer starts, after which old listener is removed from workers
- **If listener doesn't exist:**
  - Same path as initial configuration (section 1.1)

### 1.3 Listener Removal via LDS

**When:** Control plane signals listener should be removed

**Code Path:**

- `LdsApiImpl::onConfigUpdate()` receives updated listener list without the listener
- Calls `ListenerManagerImpl::removeListener(name)` for removed listener
- Looks up listener in `active_listeners_` map by name
- Moves listener from `active_listeners_` to `draining_listeners_`
- Creates `DrainingListener` wrapper with drain completion callback
- Starts drain sequence via `startDrainSequence(server.options().drainTime())`
- **Drain timer expires:**
  - Calls `Worker::removeListener()` on all worker threads
  - Each worker stops accepting on `ActiveTcpListener`
  - Existing connections on that listener are marked for draining via `startDraining()`
  - Workers call drain completion callback when done
- **All workers report completion:**
  - Listener is removed from `draining_listeners_` list
  - `ListenerImpl` destructor runs, cleaning up all resources

---

## 2. Connection Time Scenarios

### 2.1 TCP Connection Accept (Happy Path)

**When:** New TCP connection arrives at listening socket

**Code Path:**

- Kernel signals listen socket is readable (new connection pending)
- `TcpListenerImpl::onSocketEvent()` wakes up in event loop
- Calls `::accept4()` to accept socket from kernel
- Creates `ConnectionSocketImpl` wrapping accepted file descriptor
- Calls `ActiveTcpListener::onAccept(socket)` with accepted socket
- **Inside `ActiveTcpListener::onAccept()`:**
  - Checks if listener is at connection limit via `listenerConnectionLimitReached()`
  - If at limit, rejects connection and increments `downstream_cx_overflow` stat
  - Creates `ActiveTcpSocket` to manage this socket through listener filter phase
  - Inserts `ActiveTcpSocket` into `accepted_sockets_` list
  - Calls `ActiveTcpSocket::startFilterChain()` to begin listener filter processing
- **Inside `ActiveTcpSocket::startFilterChain()`:**
  - Starts `listener_filter_timer_` with timeout from `listener_filters_timeout` config
  - Iterates through `listener_filters_` list (e.g., TLS Inspector, Proxy Protocol filter)
  - For each filter, calls `GenericListenerFilter::onAccept()`
  - **Inside `GenericListenerFilter::onAccept()`:**
    - Evaluates filter's matcher predicate against socket metadata
    - If predicate doesn't match, skips this filter and continues to next
    - If predicate matches, calls actual filter's `onAccept()` method
- **Example: TLS Inspector filter runs:**
  - Calls `recv(..., MSG_PEEK)` to peek at first bytes without consuming them
  - Parses TLS ClientHello message to extract SNI, ALPN, and JA3 hash
  - Calls `socket->setRequestedServerName(sni)` to populate SNI
  - Calls `socket->setDetectedTransportProtocol("tls")`
  - Calls `socket->setRequestedApplicationProtocols(alpns)`
  - Returns `FilterStatus::Continue` to proceed to next filter
- **All listener filters complete:**
  - Cancels `listener_filter_timer_` since filters finished in time
  - Calls `ActiveTcpSocket::newConnection()` to find matching filter chain
- **Inside `ActiveTcpSocket::newConnection()`:**
  - Calls `FilterChainManagerImpl::findFilterChain(socket, stream_info)`
  - **Inside `findFilterChain()`:** (critical matching logic)
    - Extracts connection metadata: local address, remote address, SNI, transport protocol, ALPN
    - Walks nested trie structure level by level:
      1. Matches destination port (exact match)
      2. Matches destination IP prefix (LcTrie longest prefix match)
      3. Matches SNI (exact match first, then wildcard `*.example.com`, then empty catch-all)
      4. Matches transport protocol (`tls`, `raw_buffer`, etc.)
      5. Matches ALPN (e.g., `h2`, `http/1.1`)
      6. Matches direct source IP (before XFF translation)
      7. Matches source type (`LOCAL`, `EXTERNAL`, `ANY`)
      8. Matches source IP prefix (LcTrie)
      9. Matches source port
    - Returns first matching `FilterChainImpl*` or `default_filter_chain_` if no match
    - If no default configured, returns `nullptr` → connection rejected
  - If `findFilterChain()` returns `nullptr`, closes socket and increments `no_filter_chain_match` stat
  - Otherwise, calls `ActiveTcpListener::newActiveConnection(filter_chain, socket)`
- **Inside `ActiveTcpListener::newActiveConnection()`:**
  - Retrieves `TransportSocketFactory` from `FilterChainImpl::transportSocketFactory()`
  - Calls `transport_socket_factory->createTransportSocket()` to create TLS or raw transport socket
  - Creates `ConnectionImpl` wrapping IO handle and transport socket
  - Retrieves network filter factories list from `FilterChainImpl::networkFilterFactories()`
  - Calls `ListenerImpl::createNetworkFilterChain()` with connection and factories
  - **Inside `createNetworkFilterChain()`:**
    - Calls `FilterChainUtility::buildFilterChain()` with connection and filter factory list
    - For each factory callback in list, calls `factory(connection)` to instantiate filter (see section 5)
    - Each filter calls `connection->addReadFilter(this)` to register itself
  - Creates `ActiveTcpConnection` wrapper linking connection to listener
  - Inserts into `connections_by_context_[filter_chain]` map for tracking
  - Increments connection stats: `downstream_cx_total`, `downstream_cx_active`
  - Connection is now established and ready to receive data

### 2.2 Connection Accept with Listener Filter Timeout

**When:** Listener filters take too long to process (e.g., TLS Inspector waiting for ClientHello)

**Code Path:**

- Socket accepted, `ActiveTcpSocket::startFilterChain()` called (same as 2.1)
- `listener_filter_timer_` is armed with timeout duration
- Listener filters start processing but don't complete in time
- **Timer fires:** `ActiveTcpSocket::onTimeout()` is called
  - Checks `continue_on_listener_filters_timeout` config flag
  - **If `continue_on_listener_filters_timeout == true`:**
    - Calls `continueFilterChain(true)` to proceed with partial metadata
    - Filter chain matching happens with whatever metadata was collected so far
    - Connection proceeds normally (may match less specific filter chain)
  - **If `continue_on_listener_filters_timeout == false`:**
    - Calls `continueFilterChain(false)` to reject connection
    - Closes socket immediately
    - Increments `downstream_listener_filter_timeout` stat
- Removes `ActiveTcpSocket` from `accepted_sockets_` list

### 2.3 Connection Rejected Due to No Filter Chain Match

**When:** Connection metadata doesn't match any filter chain and no default is configured

**Code Path:**

- Connection proceeds through listener filters successfully
- `FilterChainManagerImpl::findFilterChain()` walks entire trie but finds no match
- No `default_filter_chain_` is configured, so returns `nullptr`
- `ActiveTcpSocket::newConnection()` receives `nullptr` filter chain
- Logs warning: "closing connection: no matching filter chain found"
- Closes socket via `socket->close()`
- Increments stat: `listener.<address>.downstream_cx_no_filter_chain_match`
- Removes `ActiveTcpSocket` from `accepted_sockets_` list

### 2.4 Connection Balancing to Different Worker

**When:** `connection_balance_config` is set to `exact_balance` and connection is rebalanced

**Code Path:**

- Connection accepted on worker A's `ActiveTcpListener`
- `ActiveTcpListener::onAccept()` checks if listener is overloaded via `numConnections()`
- Calls `ConnectionBalancer::pickTargetHandler()` to select target worker
- **If target is different worker (worker B):**
  - Calls `balancedHandlerByAddress()` to get worker B's connection handler
  - Calls `worker_b_handler->post(socket)` to send socket to worker B
  - **On worker B:** dispatcher thread wakes up from posted event
  - Calls `ActiveTcpListener::onAcceptWorker(socket)` with rebalanced flag set
  - Connection processing continues from there (listener filters, filter chain matching, etc.)
- **If target is current worker:**
  - Proceeds directly to `ActiveTcpSocket::startFilterChain()` on current worker

---

## 3. Request Time Scenarios

### 3.1 HTTP Request Processing Through Network Filters

**When:** Data arrives on established connection with HTTP filter chain

**Code Path:**

- Kernel signals connection socket is readable
- `ConnectionImpl::onReadReady()` is called by event dispatcher
- Calls `transport_socket_->doRead()` to read encrypted/raw data from socket
- **If TLS transport socket:**
  - Decrypts data using SSL context
  - Writes plaintext to connection's read buffer
- **If raw transport socket:**
  - Copies data directly to connection's read buffer
- Calls `FilterManagerImpl::onRead()` with read buffer
- **Inside `FilterManagerImpl`:**
  - Iterates through registered read filters in order (e.g., TCP Proxy, HTTP Connection Manager)
  - Calls `filter->onData(data, end_stream)` on first filter
  - Filter processes data and returns `FilterStatus`:
    - `Continue` → proceeds to next filter
    - `StopIteration` → pauses filter chain (filter will call `continueReading()` later)
- **Example: HTTP Connection Manager filter:**
  - Parses HTTP/1.1 or HTTP/2 from buffer using codec
  - Calls `ActiveStream::decodeHeaders()` when headers complete
  - HTTP router filter (inside HCM) selects upstream cluster
  - Creates upstream connection, sends request
  - Response flows back, encoded by codec, written to connection
- Data flows through write filters on response path
- Finally written to socket via `transport_socket_->doWrite()`

### 3.2 Filter Chain Modification During Active Connections

**When:** Listener config updated with new filter chains while connections are active

**This is NOT a request-time scenario but a configuration-time scenario that affects active connections**

**Code Path:**

- Covered in sections 1.2 and 6 (Draining Process)
- Active connections continue using their original `FilterChainImpl` and network filters
- New connections use new `FilterChainImpl`
- Old filter chains are drained after grace period

---

## 4. Filter Factory Creation

### 4.1 When Are Filter Factories Created?

**Timing:** At listener configuration time, NOT at connection time

**Why Factories:**
- **Performance**: Parsing and validating filter config is expensive
- **Memory**: Shared configuration reduces per-connection memory overhead
- **Consistency**: All connections on a filter chain use identical configuration
- **Hot reload**: New filter chain can be created while old one continues serving

**Factory Pattern Benefits:**
- Config is parsed once, validated once, stored once
- Factory callback is lightweight - just captures shared pointers to config
- Each connection gets its own filter instance with unique per-connection state
- Filter instances can be created/destroyed rapidly without config overhead

**Code Path:**

- During `ListenerImpl::buildFilterChains()` (config time)
- `ListenerFilterChainFactoryBuilder::buildFilterChain()` is called for each filter chain
- Calls `ProdListenerComponentFactory::createNetworkFilterFactoryList()`
- **Inside `createNetworkFilterFactoryList()`:**
  - Loops through each `filter` entry in filter chain proto
  - Looks up filter implementation in extension registry by name (e.g., `envoy.filters.network.http_connection_manager`)
  - Calls `factory.createFilterFactoryFromProto(filter_config, context)`
  - **Inside `createFilterFactoryFromProto()`:** (example for HCM)
    - Parses and validates filter-specific config proto
    - Creates any shared resources (e.g., HTTP codec stats, router config)
    - **Returns a factory callback lambda:** `[config](FilterManager& manager) { manager.addReadFilter(std::make_unique<HttpConnectionManager>(config)); }`
  - Appends factory callback to `filter_factories_` list
- **Result:** List of factory callbacks stored in `FilterChainImpl::filters_factory_`
- These factory callbacks are invoked later at connection time to create actual filter instances

### 4.2 What Do Filter Factories Contain?

**Filter factories are callback closures that capture:**

- Parsed and validated filter configuration
- Shared resources (stats scopes, route configs, codec settings)
- Factory context references (access to cluster manager, runtime, etc.)

**Filter factories do NOT contain:**

- Actual filter instances (those are created per-connection)
- Connection-specific state
- Request-specific data

### 4.3 Listener Filter Factories

**Timing:** Also created at config time

**Code Path:**

- During `ListenerImpl` constructor
- Calls `ProdListenerComponentFactory::createListenerFilterFactoryList()`
- **For each listener filter in config:**
  - Looks up filter in registry (e.g., `envoy.filters.listener.tls_inspector`)
  - Calls `factory.createListenerFilterFactoryFromProto(config, context)`
  - Returns factory callback that creates listener filter instance
- Factory callbacks stored in `ListenerImpl::listener_filter_factories_`
- Invoked at connection accept time to create listener filter instances

---

## 5. Filter Instance Creation

### 5.1 When Are Filter Instances Created?

**Timing:** At connection establishment time, NOT at config time

**Once per connection** - filters are NOT reused across connections

**Why Per-Connection Instances:**
- **Isolation**: Each connection needs independent state (buffers, codec state, upstream connections)
- **Thread safety**: No locks needed if filters aren't shared
- **Memory management**: Filter lifecycle tied to connection lifecycle - automatic cleanup
- **Debugging**: Connection-specific filters make debugging easier (no shared state to untangle)

**Performance Considerations:**
- Filter creation is fast because heavy config parsing was done at factory creation time
- Modern allocators efficiently handle frequent small allocations
- Memory overhead is acceptable given isolation and safety benefits
- Filters are destroyed immediately when connection closes - no lingering resources

### 5.2 Network Filter Instance Creation

**Code Path:**

- During `ActiveTcpListener::newActiveConnection()` (connection time)
- After filter chain is matched and `ConnectionImpl` is created
- Calls `ListenerImpl::createNetworkFilterChain(connection, filter_factories)`
- Calls `FilterChainUtility::buildFilterChain(connection, filter_factories)`
- **Inside `buildFilterChain()`:**
  - Loops through each factory callback in `filter_factories` list
  - Calls `factory(connection)` to invoke the factory
  - **Inside the factory callback:**
    - Creates new filter instance: `auto filter = std::make_unique<HttpConnectionManager>(config)`
    - Calls `connection.addReadFilter(filter)` to register filter
    - Filter is now owned by connection's filter manager
  - Repeats for all filters in chain
- **Result:** Connection now has its own instances of all network filters
- Each filter has its own per-connection state

### 5.3 Listener Filter Instance Creation

**Code Path:**

- During `ActiveTcpSocket` constructor (accept time, before filter chain matching)
- Loops through `listener_filter_factories_` from `ListenerImpl`
- **For each factory:**
  - Calls `factory(*this)` to invoke factory callback
  - Factory creates new listener filter instance
  - Filter registers itself with `ActiveTcpSocket`
- Listener filters are stored in `ActiveTcpSocket::listener_filters_` list
- **After all listener filters run and connection is established:**
  - `ActiveTcpSocket` is destroyed
  - Listener filter instances are destroyed with it
- Listener filters are short-lived (only during accept phase)

### 5.4 Filter Instance Lifecycle

**Network filters:**
- Created: when connection is established (after filter chain match)
- Lifetime: entire duration of connection
- Destroyed: when connection closes (client disconnect, error, drain, etc.)
- Each connection gets its own filter instances

**Listener filters:**
- Created: when socket is accepted (before filter chain match)
- Lifetime: during listener filter phase only (typically milliseconds)
- Destroyed: when filter chain matching completes or timeout occurs
- Each accepted socket gets its own listener filter instances

### 5.5 HTTP Filter Creation (Nested)

**HTTP filters are created by HTTP Connection Manager filter:**

**Code Path:**

- Connection established, HCM network filter instance created
- Client sends HTTP request
- HCM creates `ActiveStream` for the request
- Calls `FilterManager::createFilterChain()` for HTTP filter chain
- **For each HTTP filter factory in HCM config:**
  - Calls HTTP filter factory callback
  - Creates HTTP filter instance (e.g., Router filter, Fault filter)
  - Calls `filter_manager.addStreamDecoderFilter(filter)`
- HTTP filter instances live for the duration of the request/response
- When response completes, HTTP filters are destroyed
- HCM network filter persists for multiple requests on same connection

---

## 6. Draining Process During Filter Chain Updates

### 6.1 Filter-Chain-Only Update Trigger

**When:** LDS update changes only filter chains, not listener socket config

**Why Filter Chain Draining:**
- **Zero-downtime updates**: New connections use new config immediately
- **Graceful migration**: Existing connections complete on old config
- **Minimal disruption**: Only affected filter chains are drained, not entire listener
- **Predictable behavior**: Drain timeout provides upper bound on migration time

**Detection:**

- `ListenerMessageUtil::filterChainOnlyChange()` compares old and new listener configs
- Returns `true` if all fields match except `filter_chains` and `default_filter_chain`
- This allows in-place update without draining all connections

### 6.2 Determining Which Filter Chains to Drain

**Code Path:**

- `ListenerManagerImpl::drainFilterChains(old_listener, new_listener)` is called
- Creates `DrainingFilterChainsManager` to manage drain lifecycle
- Calls `old_listener.diffFilterChain(new_listener, callback)`
- **Inside `diffFilterChain()`:**
  - Compares `fc_contexts_` (map of filter chain proto → FilterChainImpl) between old and new listeners
  - For each filter chain in old listener:
    - Checks if identical filter chain exists in new listener (by proto hash)
    - **If filter chain NOT in new listener:** calls callback to drain it
    - **If filter chain IS in new listener:** keeps it active (reused)
- Callback calls `filter_chain.startDraining()` for each removed chain

### 6.3 Marking Filter Chains as Draining

**Code Path:**

- For each filter chain to be drained:
  - Calls `FilterChainImpl::startDraining()`
  - **Inside `FilterChainImpl::startDraining()`:**
    - Calls `factory_context_->startDraining()` on `PerFilterChainFactoryContextImpl`
    - **Inside `PerFilterChainFactoryContextImpl::startDraining()`:**
      - Sets `is_draining_.store(true)` (atomic flag)
  - Adds `FilterChainImpl` to `DrainingFilterChainsManager::draining_filter_chains_` list
- Updates stat: `listener_manager.total_filter_chains_draining` (adds count of draining chains)

### 6.4 Drain Sequence Timeline

**Code Path:**

- `DrainingFilterChainsManager::startDrainSequence(drain_time, dispatcher, completion_callback)` is called
- `drain_time` comes from `--drain-time-s` server option (default 600s / 10 minutes)
- **Drain timer is started:** fires after `drain_time` elapses
- **During drain period:**
  - **New connections:** use new filter chains from updated listener
  - **Existing connections on draining chains:** continue running normally
  - Drain decision can be queried via `PerFilterChainFactoryContextImpl::drainClose()`
  - Filters can check if they should proactively close connections (e.g., during drain)
- **Drain timer fires after drain period:**
  - Calls `Worker::removeFilterChains()` on all worker threads
  - Each worker removes draining filter chains from `ActiveTcpListener::connections_by_context_` map
  - **For each connection on draining filter chain:**
    - Connection's `ActiveTcpConnection::onEvent(RemoteClose)` is triggered
    - Connection gracefully shuts down
    - Filters are destroyed
  - Worker calls completion callback when all connections are closed

### 6.5 Completing the Drain

**Code Path:**

- Each worker calls drain completion callback from `removeFilterChains()`
- Callback posted back to main thread
- `DrainingFilterChainsManager::decWorkersPendingRemoval()` decrements worker counter
- **When last worker reports completion:**
  - Updates stat: `listener_manager.total_filter_chains_draining` (subtracts count)
  - Removes `DrainingFilterChainsManager` from `draining_filter_chains_manager_` list
  - **`FilterChainImpl` and all associated resources are destroyed:**
    - `PerFilterChainFactoryContextImpl` destroyed
    - `TransportSocketFactory` destroyed
    - Factory callbacks cleared
    - Filter chain removed from memory

### 6.6 Comparison: Full Listener Drain vs Filter Chain Drain

**Full Listener Drain (when socket config changes):**
- Entire listener stops accepting new connections
- Listener moved to `draining_listeners_` list
- All connections drained regardless of filter chain
- After drain time, listener completely removed from workers
- More disruptive, causes connection churn

**Filter Chain Drain (when only filter chains change):**
- Listener continues accepting new connections
- Only specific filter chains marked as draining
- Connections on unchanged filter chains are NOT affected
- Only connections on removed filter chains are drained
- Less disruptive, minimizes connection churn
- Allows gradual rollout of filter chain changes

### 6.7 Draining Multiple Filter Chains Simultaneously

**Scenario:** Multiple LDS updates in quick succession

**Code Path:**

- Each LDS update that triggers filter chain drain creates separate `DrainingFilterChainsManager` instance
- Multiple drain managers can exist simultaneously in `draining_filter_chains_manager_` list
- Each has its own drain timer and worker completion tracking
- They operate independently and complete on their own timelines
- Stat `listener_manager.total_filter_chains_draining` reflects sum of all draining chains across all drain managers

---

## Summary

### Key Takeaways

1. **Config time:** Filter factories are created, stored in `FilterChainImpl`, inserted into matching trie
2. **Connection time:** Filter chain is matched, filter instances are created from factories, connection is established
3. **Request time:** Data flows through network filter chain, filters process and forward
4. **Filter factories:** Created once at config time, reused for all connections
5. **Filter instances:** Created per-connection, destroyed when connection closes
6. **Draining:** Gradual removal of old filter chains while preserving active connections, with configurable grace period

### Variable/Class Quick Reference

| Name | Purpose | Lifetime |
|------|---------|----------|
| `FilterChainManagerImpl` | Manages filter chain matching trie | Per listener |
| `FilterChainImpl` | Holds transport socket factory and filter factories | Per filter chain |
| `PerFilterChainFactoryContextImpl` | Factory context for filter chain | Per filter chain |
| `filter_factories_` | List of factory callbacks | Created at config time |
| `ActiveTcpListener` | Accepts connections on worker thread | Per listener per worker |
| `ActiveTcpSocket` | Manages socket through listener filter phase | Per accepted socket (short-lived) |
| `listener_filters_` | List of listener filter instances | Per accepted socket |
| `ActiveTcpConnection` | Wraps established connection | Per connection |
| `connections_by_context_` | Map of filter chain → active connections | Per listener, tracks connections |
| `draining_filter_chains_manager_` | List of drain managers | Per filter chain update |
| `is_draining_` | Atomic flag indicating drain state | Per filter chain factory context |

---

## Navigation

| Document | Topics |
|----------|--------|
| [Part 1](OVERVIEW_PART1_architecture.md) | Architecture, ListenerManagerImpl, Worker Dispatch, Lifecycle |
| [Part 2](OVERVIEW_PART2_filter_chains.md) | Filter Chain Manager, Matching, ListenerImpl Config |
| [Part 3](OVERVIEW_PART3_active_tcp.md) | ActiveTcpListener, ActiveTcpSocket, Listener Filters, Connection Tracking |
| [Part 4](OVERVIEW_PART4_lds_and_advanced.md) | LDS API, UDP, Draining, Internal Listeners, Advanced Topics |
| **This document** | Code path scenarios for config, connection, request, factory creation, and draining |
