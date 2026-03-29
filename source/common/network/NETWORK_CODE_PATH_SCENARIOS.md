# Network Layer — Code Path Scenarios

**Directory:** `source/common/network/`
**Purpose:** Plain-text descriptions of code paths through Envoy's core network layer

---

## Table of Contents

1. [Server Connection Creation](#1-server-connection-creation)
2. [Client Connection Creation](#2-client-connection-creation)
3. [Data Read Path](#3-data-read-path)
4. [Data Write Path](#4-data-write-path)
5. [Connection Close Scenarios](#5-connection-close-scenarios)
6. [TCP Listener Accept Flow](#6-tcp-listener-accept-flow)
7. [Transport Socket Operations](#7-transport-socket-operations)
8. [Filter Manager Iteration](#8-filter-manager-iteration)
9. [Watermark and Backpressure](#9-watermark-and-backpressure)
10. [Socket and Address Creation](#10-socket-and-address-creation)

---

## 1. Server Connection Creation

### 1.1 From Accepted Socket to Established Connection

**When:** Listener manager creates connection from accepted socket after filter chain matching

**Code Path:**

- `ActiveTcpListener::newActiveConnection()` in listener_manager is called with matched filter chain and accepted socket
- Retrieves `DownstreamTransportSocketFactory` from `FilterChainImpl::transportSocketFactory()`
- Calls `transport_socket_factory->createDownstreamTransportSocket()`
- **Creates transport socket:**
  - For TLS: creates `SslSocket` wrapping the IO handle
  - For raw TCP: creates `RawBufferSocket` wrapping the IO handle
- **Creates `ServerConnectionImpl`:**
  ```cpp
  auto connection = std::make_unique<ServerConnectionImpl>(
      dispatcher,
      std::move(accepted_socket),      // ConnectionSocketPtr
      std::move(transport_socket),     // TransportSocketPtr
      stream_info,                     // StreamInfo reference
      true                             // connected = true (socket already accepted)
  );
  ```
- **Inside `ServerConnectionImpl` constructor:**
  - Calls parent `ConnectionImpl` constructor
  - Sets `transport_connect_timeout_` from filter chain config
  - If timeout configured, starts `transport_connect_timer_`
- **Inside `ConnectionImpl` constructor:**
  - Assigns unique connection ID from `next_global_id_` (atomic counter)
  - Stores references to dispatcher, socket, transport socket, stream info
  - Creates `FilterManagerImpl` instance
  - Creates watermark read and write buffers
  - **Registers file event with dispatcher:**
    ```cpp
    socket_->ioHandle().initializeFileEvent(
        dispatcher_,
        [this](uint32_t events) { return onFileEvent(events); },
        trigger_type,
        Event::FileReadyType::Read | Event::FileReadyType::Write
    );
    ```
  - Calls `transport_socket_->setTransportSocketCallbacks(*this)` to link transport socket back
  - Sets connection ID and SSL info in socket's connection info provider
- **Back in `ActiveTcpListener`:**
  - Retrieves network filter factories list from `FilterChainImpl::networkFilterFactories()`
  - Calls `FilterChainUtility::buildFilterChain()` to create filter instances
  - **For each network filter factory:**
    - Factory callback is invoked: `factory(connection)`
    - Filter instance is created (e.g., `new HttpConnectionManager(config)`)
    - Filter calls `connection->addReadFilter(this)` to register itself
    - `ConnectionImpl::addReadFilter()` delegates to `filter_manager_.addReadFilter(filter)`
    - **Inside `FilterManagerImpl::addReadFilter()`:**
      - Creates `ActiveReadFilter` wrapper around filter
      - Inserts into `upstream_filters_` list (yes, confusingly named for downstream too)
      - Calls `filter->initializeReadFilterCallbacks(active_filter)` to link filter to manager
- **Initialize filters:**
  - `connection->initializeReadFilters()` is called
  - Delegates to `filter_manager_.initializeReadFilters()`
  - **For each read filter:**
    - Calls `filter->onNewConnection()`
    - Filter returns `FilterStatus::Continue` or `StopIteration`
    - If `Continue`, proceeds to next filter
    - If `StopIteration`, saves current position and stops iteration
- **Connection is now ready:**
  - Socket file descriptor is registered in event loop
  - Transport socket is initialized
  - Network filters are instantiated and initialized
  - Connection awaits data from client

### 1.2 Transport Socket Connect Timeout (Server Side)

**When:** TLS handshake takes too long after accept

**Code Path:**

- `ServerConnectionImpl` constructor starts `transport_connect_timer_` if timeout configured
- **Timer expires before handshake completes:**
  - `ServerConnectionImpl::onTransportConnectTimeout()` is called
  - Logs error: "transport socket timeout was reached"
  - Calls `transport_socket_->closeSocket(ConnectionEvent::LocalClose)`
  - Transport socket (e.g., `SslSocket`) aborts handshake
  - Calls `connection->close(ConnectionCloseType::NoFlush)` to close immediately
  - Connection moves to `Closed` state
  - Callbacks notified with `ConnectionEvent::LocalClose`

---

## 2. Client Connection Creation

### 2.1 Initiating Outbound Connection

**When:** Upstream connection pool needs to connect to backend server

**Code Path:**

- Connection pool (e.g., `HttpConnPoolImpl`) calls `ClientConnectionFactory::createClientConnection()`
- **Inside `DefaultClientConnectionFactory::createClientConnection()`:**
  - Checks if Happy Eyeballs is enabled (multiple addresses, connection options)
  - **If Happy Eyeballs enabled:**
    - Creates `HappyEyeballsConnectionImpl` to try multiple addresses
    - Returns Happy Eyeballs wrapper
  - **If single address:**
    - Proceeds to create standard `ClientConnectionImpl`
- **Create client socket:**
  - Calls `SocketImpl::createSocket()` via socket interface
  - Creates `IoSocketHandleImpl` wrapping new file descriptor from `socket()` syscall
  - Socket is in non-blocking mode
  - Creates `ConnectionSocketImpl` wrapping IO handle
- **Apply socket options:**
  - Iterates through `options.socket_options_` list
  - Calls `option->setOption(socket, ...)` for each
  - Common options: `TCP_NODELAY`, `SO_KEEPALIVE`, `SO_SNDBUF`, `SO_RCVBUF`
- **Create transport socket:**
  - Calls `UpstreamTransportSocketFactory::createTransportSocket(options)`
  - Options include SNI, ALPN from cluster config
  - For TLS: creates `SslSocket` with client SSL context
  - For raw: creates `RawBufferSocket`
- **Create `ClientConnectionImpl`:**
  ```cpp
  auto connection = std::make_unique<ClientConnectionImpl>(
      dispatcher,
      std::move(socket),
      std::move(transport_socket),
      std::move(options),
      transport_socket_options
  );
  ```
- **Inside `ClientConnectionImpl` constructor:**
  - Calls parent `ConnectionImpl` constructor with `connected=false`
  - Sets `connecting_ = true` in parent
  - Stores source/destination addresses
  - **Does NOT yet call `connect()` - that's done separately**

### 2.2 Connect Syscall and Handshake

**When:** `ClientConnection::connect()` is called after construction

**Code Path:**

- Connection pool calls `connection->connect()`
- **Inside `ClientConnectionImpl::connect()`:**
  - Calls `ioHandle().connect(destination_address)`
  - **Inside `IoSocketHandleImpl::connect()`:**
    - Calls `::connect()` syscall with destination sockaddr
    - Since socket is non-blocking, typically returns `EINPROGRESS`
  - Returns `Api::SysCallIntResult` with errno
- **If `connect()` returns `EINPROGRESS`:**
  - Connection state remains `Open` with `connecting_ = true`
  - File event for `Write` is already registered (from constructor)
  - Event loop will notify when socket becomes writable (connection established)
- **Event loop wakes up with socket writable:**
  - `ConnectionImpl::onFileEvent(Event::FileReadyType::Write)` is called
  - **Inside `onFileEvent()`:**
    - Checks `connecting_` flag
    - Calls `ioHandle().supportedGetSocketOption()` with `SO_ERROR`
    - Reads socket error to check if connection succeeded
    - **If `SO_ERROR == 0` (connection succeeded):**
      - Sets `connecting_ = false`
      - Calls `transport_socket_->onConnected()` to start handshake
      - **For TLS transport socket:**
        - Initiates TLS handshake by calling `SSL_do_handshake()`
        - May complete immediately or require more I/O
      - **For raw buffer socket:**
        - No-op, just marks as ready
      - Calls `raiseConnectionEvent(ConnectionEvent::Connected)`
      - Notifies all registered connection callbacks
    - **If `SO_ERROR != 0` (connection failed):**
      - Calls `closeSocket(ConnectionEvent::RemoteClose)`
      - Notifies callbacks with failure

### 2.3 Happy Eyeballs Flow

**When:** Connecting to host with multiple addresses (IPv6 and IPv4)

**Code Path:**

- `HappyEyeballsConnectionImpl` receives list of addresses
- Sorts addresses by family (IPv6 first per RFC 8305)
- **Starts first connection attempt:**
  - Creates `ClientConnectionImpl` for first address (typically IPv6)
  - Calls `connection->connect()`
  - Starts "connection attempt delay" timer (default 300ms)
- **Timer expires before first connection succeeds:**
  - Starts second connection attempt in parallel (IPv4 address)
  - Creates another `ClientConnectionImpl`
  - Calls `connection->connect()` on second
  - Now two connections racing
- **First connection to complete wins:**
  - Whichever `ConnectionEvent::Connected` callback fires first
  - That connection becomes the "active" connection
  - Other connection is closed via `connection->close(ConnectionCloseType::NoFlush)`
  - `HappyEyeballsConnectionImpl` delegates all further calls to winning connection
- **If all connections fail:**
  - Last failure triggers `ConnectionEvent::RemoteClose` on wrapper
  - Connection pool sees connection failure

---

## 3. Data Read Path

### 3.1 Kernel to Filter Chain

**When:** Data arrives from remote peer

**Code Path:**

- **Kernel signals socket is readable:**
  - Data arrives in kernel TCP receive buffer
  - `epoll_wait()` / `kevent()` returns in event loop
  - Event loop invokes registered callback
- **Dispatcher calls connection's file event callback:**
  - `ConnectionImpl::onFileEvent(Event::FileReadyType::Read)` is invoked
- **Inside `onFileEvent()`:**
  - Checks if read is disabled via `read_disable_count_`
  - If disabled, ignores read event and returns
  - Calls `transport_socket_->doRead(*read_buffer_)`
- **Inside `TransportSocket::doRead()` (e.g., `SslSocket` for TLS):**
  - **For TLS:**
    - Calls `SSL_read()` from BoringSSL
    - BoringSSL decrypts data from socket's IO handle
    - Writes decrypted plaintext into `read_buffer_`
  - **For raw TCP:**
    - Calls `ioHandle().read(*buffer)` directly
    - **Inside `IoSocketHandleImpl::read()`:**
      - Reserves space in buffer
      - Calls `::readv()` syscall with iovec array
      - Appends read bytes to buffer
  - Returns `IoResult{bytes_read, end_stream}`
- **Check result:**
  - If `bytes_read > 0` or `end_stream == true`:
    - Calls `filter_manager_.onRead()` to process data
  - If `bytes_read == 0` and not end_stream:
    - No data available yet, returns and waits for next event
  - If error (e.g., `ECONNRESET`):
    - Calls `closeSocket(ConnectionEvent::RemoteClose)`
- **Inside `FilterManagerImpl::onRead()`:**
  - Checks if iteration is already in progress (`iteration_state_`)
  - **Iterates through read filters:**
    - Gets first filter (or resumes from stopped filter)
    - Calls `ActiveReadFilter::onData(*read_buffer_, end_stream)`
    - **Inside filter's `onData()`:**
      - Filter processes data in buffer
      - May consume some/all data
      - May produce transformed data
      - Returns `FilterStatus::Continue` or `StopIteration`
    - **If filter returns `Continue`:**
      - Advances to next filter in list
      - Repeats `onData()` call on next filter
    - **If filter returns `StopIteration`:**
      - Saves current filter position
      - Breaks out of iteration loop
      - Filter will later call `callbacks->continueReading()` to resume
  - If all filters return `Continue`:
    - Iteration completes, all data processed
  - Drains processed data from `read_buffer_`
- **Example flow through network filters:**
  1. **TCP Proxy filter:**
     - Forwards all data directly to upstream connection
     - Returns `Continue` (even though it consumed data)
  2. **HTTP Connection Manager filter:**
     - Parses HTTP headers and body from buffer
     - For each complete HTTP request:
       - Creates `ActiveStream`
       - Runs HTTP filter chain (separate from network filter chain)
       - HTTP Router filter selects upstream and forwards request
     - Returns `Continue` to allow other network filters (if any)

### 3.2 Transport Socket Wants Read

**When:** TLS handshake or renegotiation needs more data

**Code Path:**

- During `SSL_do_handshake()` or `SSL_read()`, BoringSSL returns `SSL_ERROR_WANT_READ`
- `SslSocket::doRead()` sets `transport_wants_read_ = true` in `ConnectionImpl`
- Even if no plaintext data was decrypted, event loop continues monitoring socket
- Next read event:
  - `onFileEvent()` checks `transport_wants_read_`
  - Calls `doRead()` again even if no filter is waiting
  - Allows TLS handshake to continue

---

## 4. Data Write Path

### 4.1 Filter Chain to Kernel

**When:** Application wants to send data (e.g., HTTP response)

**Code Path:**

- **Filter calls `connection->write(buffer, end_stream)`:**
  - Could be HTTP Connection Manager writing response
  - Could be TCP Proxy forwarding upstream data
- **Inside `ConnectionImpl::write()`:**
  - Checks if connection is closed or half-closed local
  - If already closed, drops data and returns
  - Calls `filter_manager_.startWrite(buffer, end_stream)`
- **Inside `FilterManagerImpl::onWrite()`:**
  - **Iterates through write filters in REVERSE order:**
    - Write filters are processed from last to first
    - This allows filters closest to application to transform data before filters closer to socket
  - Gets last write filter in list
  - Calls `ActiveWriteFilter::onWrite(*write_buffer_, end_stream)`
  - **Inside filter's `onWrite()`:**
    - Filter may transform data
    - Filter may compress, encrypt at application level, etc.
    - Returns `FilterStatus::Continue` or `StopIteration`
  - **If filter returns `Continue`:**
    - Moves to previous filter in list
    - Repeats `onWrite()` call
  - **If filter returns `StopIteration`:**
    - Pauses iteration
    - Filter will later call `callbacks->injectWriteDataToFilterChain()` to resume
  - If all filters return `Continue`:
    - Data is now in `write_buffer_` ready to be written to socket
- **After filter chain completes:**
  - `ConnectionImpl` appends data to its internal `write_buffer_`
  - Calls `transport_socket_->doWrite(*write_buffer_, end_stream)`
- **Inside `TransportSocket::doWrite()` (e.g., `SslSocket` for TLS):**
  - **For TLS:**
    - Calls `SSL_write()` from BoringSSL
    - BoringSSL encrypts plaintext and writes to socket
    - May not write all data if kernel buffer is full
  - **For raw TCP:**
    - Calls `ioHandle().write(*buffer)` directly
    - **Inside `IoSocketHandleImpl::write()`:**
      - Builds iovec array from buffer slices
      - Calls `::writev()` syscall
      - Returns number of bytes written
  - Returns `IoResult{bytes_written}`
- **Check result:**
  - If `bytes_written > 0`:
    - Drains written bytes from `write_buffer_`
    - Updates stats (bytes written counter)
  - If `bytes_written < buffer.length()`:
    - Some data remains in write buffer
    - Socket write buffer is full
    - Connection waits for socket to become writable again
  - If error (e.g., `EPIPE`, `ECONNRESET`):
    - Calls `closeSocket(ConnectionEvent::RemoteClose)`
- **Socket becomes writable again:**
  - Event loop calls `onFileEvent(Event::FileReadyType::Write)`
  - `ConnectionImpl` calls `doWrite()` again to flush remaining data
  - Repeats until `write_buffer_` is empty or error occurs

### 4.2 Write Buffer High Watermark

**When:** Write buffer grows beyond configured limit

**Code Path:**

- As data is appended to `write_buffer_`, buffer's watermark callbacks are checked
- **When buffer crosses high watermark:**
  - `write_buffer_` invokes high watermark callback
  - Callback is `ConnectionImpl::onWriteBufferHighWatermark()`
  - **Inside `onWriteBufferHighWatermark()`:**
    - Sets `write_buffer_above_high_watermark_ = true`
    - Calls `raiseConnectionEvent(ConnectionEvent::HighWatermark)`
    - Notifies all connection callbacks
- **Downstream filter (e.g., HTTP Connection Manager) receives callback:**
  - HTTP router stops reading from upstream connection
  - Stops decoding more requests
  - Applies backpressure up the chain
- **Write buffer drains below low watermark:**
  - `write_buffer_` invokes low watermark callback
  - Callback is `ConnectionImpl::onWriteBufferLowWatermark()`
  - Sets `write_buffer_above_high_watermark_ = false`
  - Calls `raiseConnectionEvent(ConnectionEvent::LowWatermark)`
  - HTTP router resumes reading from upstream
  - Processing continues

---

## 5. Connection Close Scenarios

### 5.1 NoFlush Close

**When:** Immediate close required (error, abort, reset)

**Code Path:**

- `connection->close(ConnectionCloseType::NoFlush)` is called
- **Inside `ConnectionImpl::close()`:**
  - Checks if socket is already closed
  - Calls `closeInternal(ConnectionCloseType::NoFlush)`
- **Inside `closeInternal()`:**
  - Checks if write buffer has pending data
  - For `NoFlush`: ignores pending data
  - Calls `closeConnectionImmediately()`
- **Inside `closeConnectionImmediately()`:**
  - Disables file events on IO handle
  - Calls `ioHandle().close()` to close file descriptor
  - Sets state to `State::Closed`
  - Calls `raiseConnectionEvent(ConnectionEvent::LocalClose)`
  - **For each connection callback:**
    - Calls `callback->onEvent(ConnectionEvent::LocalClose)`
    - Callback may be HTTP stream, health checker, etc.
  - Calls `filter_manager_.log(AccessLog::AccessLogType::TcpConnectionEnd)`
  - Writes access logs for connection
- **Result:**
  - File descriptor closed immediately
  - Any pending write data is discarded
  - TCP RST may be sent if data was pending

### 5.2 FlushWrite Close

**When:** Graceful close - wait for write buffer to drain

**Code Path:**

- `connection->close(ConnectionCloseType::FlushWrite)` is called
- **Inside `close()`:**
  - Checks close type
  - Calls `closeThroughFilterManager()` with `ConnectionCloseAction`
- **Inside `closeThroughFilterManager()`:**
  - Checks if connection close through filter manager is enabled (runtime feature)
  - Calls `filter_manager_.onConnectionClose(close_action)`
- **Inside `FilterManagerImpl::onConnectionClose()`:**
  - Sets `state_.local_close_pending_ = true`
  - Calls `maybeClose()` to check if close can proceed
- **Inside `FilterManagerImpl::maybeClose()`:**
  - Checks `filter_pending_close_count_`
  - If any filter has pending close (via `disableClose(true)`):
    - Defers close until filter calls `disableClose(false)`
    - Returns without closing
  - If no pending filters and `local_close_pending_`:
    - Calls `connection_.closeConnection(close_action)`
- **Back in `ConnectionImpl::closeConnection()`:**
  - Checks if write buffer is empty
  - **If write buffer has data:**
    - Enables write events on socket
    - Waits for `doWrite()` to drain buffer
    - Each write event attempts to flush more data
  - **When write buffer becomes empty:**
    - Calls `closeConnectionImmediately()`
    - File descriptor closed
    - Callbacks notified
  - **If write buffer was already empty:**
    - Immediately calls `closeConnectionImmediately()`

### 5.3 FlushWriteAndDelay Close

**When:** Graceful close with delay - allow peer to close first

**Code Path:**

- `connection->close(ConnectionCloseType::FlushWriteAndDelay)` is called
- Same as `FlushWrite` until write buffer is empty
- **After write buffer drains:**
  - Instead of immediate close, starts delayed close timer
  - Calls `initializeDelayedCloseTimer()`
  - **Inside `initializeDelayedCloseTimer()`:**
    - Creates timer with `delayed_close_timeout_` duration (from config)
    - Timer callback is `closeConnectionImmediately()`
  - Sets `delayed_close_state_ = DelayedCloseState::CloseAfterFlushAndWait`
  - Enables file event monitoring for `Event::FileReadyType::Closed` (if not half-close)
- **Two ways delayed close completes:**
  1. **Peer closes first (FIN received):**
     - `onFileEvent(Event::FileReadyType::Closed)` is called
     - Cancels delayed close timer
     - Calls `closeConnectionImmediately()`
     - Cleaner close since peer initiated
  2. **Timer expires:**
     - `closeConnectionImmediately()` is called from timer
     - Closes even if peer hasn't closed
     - Ensures eventual cleanup

### 5.4 Half-Close Scenarios

**When:** `enable_half_close` is true, connection can be half-closed

**Code Path:**

- **Write side closes first:**
  - `connection->write(buffer, end_stream=true)` is called
  - Sets `write_end_stream_ = true`
  - After write buffer drains, calls `transport_socket_->doWrite(buffer, true)`
  - Transport socket sends FIN on write side
  - Connection state remains `Open` (not fully closed)
  - Read side still accepts data
- **Read side receives FIN:**
  - `transport_socket_->doRead()` returns `IoResult{0, end_stream=true}`
  - Sets `read_end_stream_ = true`
  - Calls `filter_manager_.onRead()` with end_stream flag
  - Filters see `end_stream=true`, process final data
  - Connection state remains `Open` (not fully closed)
  - Can still send data on write side
- **Full close when both sides closed:**
  - When both `read_end_stream_` and `write_end_stream_` are true
  - Connection transitions to `Closing` then `Closed`
  - Both sides of connection are done

### 5.5 AbortReset Close

**When:** Immediate abort with TCP RST

**Code Path:**

- `connection->close(ConnectionCloseType::AbortReset)` is called
- **Inside `close()`:**
  - Sets detected close type to `LocalReset`
  - Sets socket option `SO_LINGER` with `{onoff=1, linger=0}`
  - This causes `close()` to send RST instead of FIN
  - Calls `closeSocket(ConnectionEvent::LocalClose)`
  - **Inside `closeSocket()`:**
    - Calls `ioHandle().close()`
    - Kernel sends TCP RST to peer
    - Peer sees connection reset
  - Notifies callbacks immediately
- **Result:**
  - TCP RST sent to peer
  - No graceful shutdown
  - Used for error conditions or security events

---

## 6. TCP Listener Accept Flow

### 6.1 From Kernel to Callback

**When:** Client connects to listening socket

**Code Path:**

- **Client initiates TCP connection:**
  - SYN packet arrives
  - Kernel completes 3-way handshake (SYN, SYN-ACK, ACK)
  - Connection enters kernel's accept queue
- **Kernel signals listen socket is readable:**
  - Socket has pending connection(s) in accept queue
  - `epoll_wait()` / `kevent()` returns
  - Event loop invokes registered callback
- **Dispatcher calls listener's file event callback:**
  - `TcpListenerImpl::onSocketEvent(Event::FileReadyType::Read)` is called
- **Inside `TcpListenerImpl::onSocketEvent()`:**
  - Loops up to `max_connections_to_accept_per_socket_event_` times (default 1024)
  - **For each iteration:**
    - Checks if socket is still open
    - Calls `socket_->ioHandle().accept(&remote_addr, &remote_addr_len)`
    - **Inside `IoSocketHandleImpl::accept()`:**
      - Calls `::accept4()` syscall with `SOCK_NONBLOCK` flag
      - Kernel returns new file descriptor for accepted connection
      - Returns `IoHandlePtr` wrapping new file descriptor
    - If `accept()` returns `nullptr`:
      - No more pending connections (`EAGAIN` / `EWOULDBLOCK`)
      - Breaks out of loop
    - **Check global connection limit:**
      - Calls `rejectCxOverGlobalLimit()`
      - If using overload manager:
        - Calls `overload_state_->tryAllocateResource(GlobalDownstreamMaxConnections, 1)`
        - Returns true if limit reached
      - If using runtime:
        - Checks `runtime_.getInteger(GlobalMaxCxRuntimeKey)`
        - Compares against `AcceptedSocketImpl::acceptedSocketCount()`
      - If limit reached:
        - Calls `io_handle->close()` to close immediately
        - Calls `cb_.onReject(GlobalCxLimit)` to increment stats
        - Continues to next iteration
    - **Check load shedding:**
      - Calls `listener_accept_->shouldShedLoad()` (overload manager)
      - Or checks `random_.bernoulli(reject_fraction_)` (random rejection)
      - If shedding:
        - Releases global connection resource
        - Closes socket
        - Calls `cb_.onReject(OverloadAction)`
        - Continues to next iteration
    - **Get local and remote addresses:**
      - If listener is on `0.0.0.0` or `::`:
        - Calls `io_handle->localAddress()` to get actual bound address
      - Otherwise uses listener's local address
      - Parses `remote_addr` sockaddr into `Address::Instance`
    - **Create `AcceptedSocketImpl`:**
      ```cpp
      auto socket = std::make_unique<AcceptedSocketImpl>(
          std::move(io_handle),
          local_address,
          remote_address,
          overload_state_,
          track_global_cx_limit_in_overload_manager_
      );
      ```
    - **Call accept callback:**
      - `cb_.onAccept(std::move(socket))`
      - `cb_` is reference to `ActiveTcpListener` in listener_manager
      - This transitions to listener filter phase (covered in listener manager docs)
  - After loop, calls `cb_.recordConnectionsAcceptedOnSocketEvent(count)` for stats
- **Result:**
  - Accepted sockets passed to listener manager
  - Listener manager runs listener filters
  - After filters, filter chain is matched
  - Connection is created (back to section 1)

---

## 7. Transport Socket Operations

### 7.1 TLS Handshake (Server Side)

**When:** Client initiates TLS connection, sends ClientHello

**Code Path:**

- Connection is created with `SslSocket` as transport socket
- `ServerConnectionImpl` starts transport connect timeout timer
- Socket is registered for read events
- **ClientHello arrives:**
  - `onFileEvent(Read)` calls `transport_socket_->doRead()`
  - **Inside `SslSocket::doRead()`:**
    - Calls `SSL_read()` from BoringSSL
    - BoringSSL state machine detects this is handshake phase
    - **BoringSSL reads ClientHello:**
      - Parses supported cipher suites
      - Parses extensions (SNI, ALPN, etc.)
      - Selects cipher suite
      - Finds matching certificate from SSL context
    - **BoringSSL writes ServerHello, Certificate, ServerKeyExchange:**
      - Constructs response messages
      - Encrypts if necessary
      - Writes to socket via bio (Berkeley I/O callbacks)
    - `SSL_read()` returns `SSL_ERROR_WANT_READ` (needs more data)
    - `doRead()` returns `IoResult{0, false}` (no plaintext yet)
- **More handshake messages exchanged:**
  - Client sends ClientKeyExchange, ChangeCipherSpec, Finished
  - Each triggers another `doRead()` call
  - BoringSSL processes and responds
- **Handshake completes:**
  - `SSL_read()` returns success or `SSL_ERROR_WANT_READ` after final messages
  - `SslSocket` calls `transport_socket_callbacks_->raiseEvent(Connected)`
  - Connection callbacks receive `ConnectionEvent::Connected`
  - Cancels transport connect timeout timer if set
  - TLS connection is established, ready for application data
- **Application data flows:**
  - Subsequent `doRead()` calls decrypt application data
  - `doWrite()` calls encrypt application data
  - All transparent to network filters

### 7.2 TLS Encryption (Write Path)

**When:** Application data needs to be encrypted and sent

**Code Path:**

- Filter calls `connection->write(buffer, end_stream)`
- Eventually reaches `SslSocket::doWrite(*write_buffer_, end_stream)`
- **Inside `SslSocket::doWrite()`:**
  - Iterates through buffer slices
  - For each slice:
    - Calls `SSL_write(ssl_, data, length)`
    - **Inside BoringSSL:**
      - Encrypts plaintext using session cipher
      - Adds TLS record header
      - Computes MAC/HMAC
      - Writes encrypted record to BIO
    - BIO callbacks write to `IoSocketHandleImpl`
    - Data flows to kernel socket buffer
  - Returns total bytes written
- **Result:**
  - Plaintext from filters is encrypted
  - Encrypted TLS records sent to peer
  - Peer's TLS stack decrypts back to plaintext

### 7.3 TLS Decryption (Read Path)

**When:** Encrypted data arrives from peer

**Code Path:**

- `onFileEvent(Read)` calls `SslSocket::doRead(*read_buffer_)`
- **Inside `SslSocket::doRead()`:**
  - Calls `SSL_read(ssl_, buffer, size)`
  - **Inside BoringSSL:**
    - Reads encrypted data from BIO
    - BIO callbacks read from `IoSocketHandleImpl`
    - Parses TLS record headers
    - Decrypts ciphertext using session cipher
    - Verifies MAC/HMAC
    - Returns decrypted plaintext
  - Writes decrypted plaintext to `read_buffer_`
  - Returns bytes of plaintext read
- **Result:**
  - Encrypted TLS records from peer are decrypted
  - Plaintext passed to filter chain
  - Filters see unencrypted application data

### 7.4 Raw Buffer Socket (No Encryption)

**When:** Plain TCP connection, no TLS

**Code Path:**

- Connection created with `RawBufferSocket` as transport socket
- **On read:**
  - `RawBufferSocket::doRead(*read_buffer_)` is called
  - **Inside `doRead()`:**
    - Calls `io_handle_->read(*read_buffer_)`
    - Directly reads from socket into buffer
    - No encryption, no transformation
    - Returns `IoResult` from IO handle
  - Plaintext data immediately available to filters
- **On write:**
  - `RawBufferSocket::doWrite(*write_buffer_, end_stream)` is called
  - **Inside `doWrite()`:**
    - Calls `io_handle_->write(*write_buffer_)`
    - Directly writes from buffer to socket
    - No encryption, no transformation
    - Returns `IoResult` from IO handle
  - Data sent as-is to peer

---

## 8. Filter Manager Iteration

### 8.1 Read Filter Iteration

**When:** Data arrives and needs processing by filter chain

**Code Path:**

- `FilterManagerImpl::onRead()` is called with data in read buffer
- **Check iteration state:**
  - If iteration is already in progress (`iteration_state_ != Idle`):
    - Queues data for later processing
    - Returns to avoid recursion
  - Sets `iteration_state_ = Processing`
- **Determine starting filter:**
  - If resuming from stopped filter:
    - Gets filter from saved position
  - If new iteration:
    - Gets first filter in `upstream_filters_` list
- **Iterate through filters:**
  - **For each `ActiveReadFilter` in list:**
    - Checks if filter has been initialized via `initialized_`
    - If not initialized:
      - Calls `filter_->onNewConnection()`
      - Sets `initialized_ = true`
      - If returns `StopIteration`:
        - Saves current position
        - Breaks iteration
    - **Call `filter_->onData()` with read buffer:**
      - Filter processes data
      - Filter may consume data from buffer
      - Filter may transform data
      - Returns `FilterStatus`
    - **Handle return status:**
      - **If `Continue`:**
        - Advances to next filter in list
        - Continues iteration
      - **If `StopIteration`:**
        - Saves current filter position
        - Sets `iteration_state_ = Idle`
        - Breaks out of loop
        - Waits for filter to call `continueReading()`
  - If reached end of filter list:
    - All filters processed data
    - Sets `iteration_state_ = Idle`
- **Resume iteration:**
  - When stopped filter calls `callbacks->continueReading()`
  - Calls `FilterManagerImpl::onContinueReading(filter, buffer_source)`
  - Resumes iteration from next filter after saved position

### 8.2 Write Filter Iteration

**When:** Data needs to be written to socket

**Code Path:**

- `FilterManagerImpl::onWrite()` is called
- **Iterates BACKWARD through write filters:**
  - Starts from last filter in `downstream_filters_` list
  - Processes toward first filter
  - This allows filters closest to application to transform first
- **For each `ActiveWriteFilter`:**
  - Calls `filter_->onWrite(*write_buffer_, end_stream)`
  - Filter processes and transforms data
  - Returns `FilterStatus`
  - **If `Continue`:**
    - Moves to previous filter
  - **If `StopIteration`:**
    - Stops iteration
    - Filter will call `injectWriteDataToFilterChain()` later
- **After all filters:**
  - Data in write buffer is ready for transport socket
  - Returns control to `ConnectionImpl` for actual write

### 8.3 Filter Callbacks

**When:** Filter needs to interact with filter manager

**Common callbacks:**

- **`continueReading()`:**
  - Called by read filter to resume iteration after `StopIteration`
  - Calls `filter_manager_.onContinueReading(this)`
  - Iteration resumes from next filter
- **`injectReadDataToFilterChain(data, end_stream)`:**
  - Called by read filter to inject custom data
  - Bypasses connection read buffer
  - Calls `filter_manager_.onContinueReading(this, data)`
  - Next filters see injected data
- **`injectWriteDataToFilterChain(data, end_stream)`:**
  - Called by write filter to inject custom data
  - Allows filter to transform data and push to next filters
  - Useful for compression, batching, etc.
- **`upstreamHost()` / `upstreamHost(host)`:**
  - Getter/setter for upstream host metadata
  - Shared state between filters
- **`disableClose(bool disable)`:**
  - Allows filter to prevent connection close
  - Filter can defer close until async operation completes
  - `disableClose(true)`: increments `filter_pending_close_count_`
  - `disableClose(false)`: decrements count, may trigger delayed close

---

## 9. Watermark and Backpressure

### 9.1 Read Buffer High Watermark

**When:** Read buffer grows beyond configured limit

**Code Path:**

- As transport socket reads data, `read_buffer_` grows
- **When buffer crosses high watermark threshold:**
  - `read_buffer_` invokes high watermark callback
  - Callback is `ConnectionImpl::onReadBufferHighWatermark()`
  - **Inside callback:**
    - Increments `read_disable_count_`
    - Disables read events on socket via `ioHandle().disableFileEvents(Read)`
    - **Result:** Event loop stops notifying connection of readable socket
- **Socket remains readable but ignored:**
  - Data accumulates in kernel TCP receive buffer
  - Kernel's TCP flow control kicks in
  - Advertises smaller TCP window to peer
  - Peer slows down sending rate
  - **Backpressure propagates to peer**
- **Buffer drains below low watermark:**
  - Filters consume and process data
  - `read_buffer_->drain()` removes processed data
  - Buffer size decreases
  - **When buffer crosses low watermark:**
    - `read_buffer_` invokes low watermark callback
    - Callback is `ConnectionImpl::onReadBufferLowWatermark()`
    - Decrements `read_disable_count_`
    - If count reaches 0:
      - Re-enables read events via `ioHandle().enableFileEvents(Read)`
      - Event loop resumes notifying of readable socket
      - Reading resumes, TCP window opens

### 9.2 Write Buffer High Watermark

**When:** Write buffer grows beyond configured limit (covered in 4.2)

**Additional details:**

- Write buffer watermark affects both downstream and upstream
- **Downstream:** HTTP Connection Manager stops reading new requests
- **Upstream:** Connection pool stops dispatching new requests
- Creates backpressure chain:
  ```
  Slow downstream client
    → Write buffer fills
    → High watermark triggered
    → HTTP router stops reading from upstream
    → Upstream buffer fills
    → Upstream slows down
  ```

### 9.3 Configuring Watermarks

**When:** Listener or cluster is configured

**Code Path:**

- `connection->setBufferLimits(limit)` is called with configured value
- **Inside `setBufferLimits()`:**
  - Calculates high and low watermark values
  - Default: high = limit, low = limit / 2
  - Calls `read_buffer_->setWatermarks(limit)`
  - Calls `write_buffer_->setWatermarks(limit)`
  - **Inside `WatermarkBuffer::setWatermarks()`:**
    - Stores high and low watermark values
    - Checks current buffer size against new watermarks
    - If above high watermark:
      - Immediately invokes high watermark callback
    - If below low watermark:
      - Invokes low watermark callback if was above

---

## 10. Socket and Address Creation

### 10.1 Creating Listen Socket

**When:** Listener is configured to bind to address

**Code Path:**

- `ListenSocketFactoryImpl` is created with address and socket type
- `getListenSocket(worker_index)` is called
- **Inside `getListenSocket()`:**
  - If socket already exists in `sockets_` array:
    - Returns existing socket for that worker
  - Otherwise creates new socket:
    - Calls `createListenSocketAndApplyOptions(worker_index)`
- **Inside `createListenSocketAndApplyOptions()`:**
  - Calls `socket_interface_->socket(socket_type, address)`
  - **Inside `SocketInterfaceImpl::socket()`:**
    - Determines socket domain: `AF_INET`, `AF_INET6`, `AF_UNIX`
    - Calls `::socket(domain, SOCK_STREAM, 0)` syscall
    - Creates `IoSocketHandleImpl` wrapping file descriptor
  - Creates `ListenSocketImpl` wrapping IO handle
  - **Apply socket options:**
    - Iterates through `options_` list
    - For each option: calls `option->setOption(socket, ...)`
    - Common options: `SO_REUSEADDR`, `SO_REUSEPORT`, `IP_FREEBIND`
  - **Bind socket:**
    - Calls `socket->bind(address)`
    - **Inside `SocketImpl::bind()`:**
      - Calls `ioHandle().bind(address)`
      - **Inside `IoSocketHandleImpl::bind()`:**
        - Converts address to sockaddr
        - Calls `::bind(fd, sockaddr, sockaddr_len)` syscall
        - Kernel binds socket to address and port
  - **Listen on socket:**
    - Calls `socket->listen(tcp_backlog_size)`
    - **Inside `SocketImpl::listen()`:**
      - Calls `::listen(fd, backlog)` syscall
      - Kernel sets socket to listening state
      - Creates accept queue with size `backlog`
  - Returns socket

### 10.2 Address Parsing and Resolution

**When:** Config specifies address string like `"0.0.0.0:8080"`

**Code Path:**

- `Utility::parseInternetAddressAndPort(address_string)` is called
- **Inside `parseInternetAddressAndPort()`:**
  - Splits string into host and port parts
  - **Parse IP address:**
    - Calls `Address::parseIpAddress(host_part)`
    - **For IPv4:**
      - Calls `::inet_pton(AF_INET, host, &in_addr)`
      - Creates `Ipv4Instance(in_addr, port)`
    - **For IPv6:**
      - Handles bracket notation `[::1]:8080`
      - Calls `::inet_pton(AF_INET6, host, &in6_addr)`
      - Creates `Ipv6Instance(in6_addr, port)`
    - **For domain name:**
      - Not an IP address literal
      - Returns error or defers to DNS resolution
  - **Special addresses:**
    - `"0.0.0.0:port"` → any IPv4 address
    - `":::port"` → any IPv6 address
- Returns `Address::InstanceConstSharedPtr`

### 10.3 Socket Option Application

**When:** Socket is created and needs configuration

**Code Path:**

- Socket options stored in `socket->options()` list
- For each `Socket::Option`:
  - Calls `option->setOption(socket, state)`
  - **Inside `SocketOptionImpl::setOption()`:**
    - Calls `socket->setSocketOption(level, name, value, size)`
    - **Inside `SocketImpl::setSocketOption()`:**
      - Calls `ioHandle().setOption(level, name, value, size)`
      - **Inside `IoSocketHandleImpl::setOption()`:**
        - Calls `::setsockopt(fd, level, optname, value, size)` syscall
        - Kernel applies socket option
- **Common socket options:**
  - `TCP_NODELAY` (disable Nagle's algorithm)
  - `SO_KEEPALIVE` (enable TCP keepalive)
  - `SO_REUSEADDR` (allow address reuse)
  - `SO_REUSEPORT` (allow port reuse across workers)
  - `SO_RCVBUF` (receive buffer size)
  - `SO_SNDBUF` (send buffer size)
  - `IP_FREEBIND` (bind to non-local addresses)
  - `IPV6_V6ONLY` (IPv6 only, no IPv4 mapping)

---

## Summary

### Key Takeaways

1. **Server connections:** Created from accepted sockets after listener filters and filter chain matching
2. **Client connections:** Created with socket in non-blocking mode, `connect()` returns immediately, completion detected via write event
3. **Read path:** Kernel → IO handle → Transport socket (decrypt) → Read buffer → Filter manager → Network filters
4. **Write path:** Network filters → Filter manager → Write buffer → Transport socket (encrypt) → IO handle → Kernel
5. **Close types:** NoFlush (immediate), FlushWrite (drain first), FlushWriteAndDelay (drain + wait), AbortReset (send RST)
6. **TLS:** BoringSSL handles handshake and encryption/decryption, transparent to network filters
7. **Filter manager:** Iterates read filters forward, write filters backward, supports stop/resume
8. **Watermarks:** High watermark disables reading/triggers backpressure, low watermark re-enables
9. **Listener accept:** Batch accepts connections, checks limits, rejects if overloaded
10. **Socket options:** Applied via `setsockopt()`, configure TCP behavior and buffer sizes

### Class Interaction Summary

```
┌─────────────────────────────────────────────────────────────┐
│ Application Layer (HTTP filters, TCP proxy, etc.)          │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ onData() / write()
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│ Network Filters (HTTP Connection Manager, TCP Proxy, etc.) │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ FilterManagerImpl::onRead/onWrite
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│ ConnectionImpl (manages buffers, events, filter manager)   │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ doRead() / doWrite()
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│ TransportSocket (SslSocket / RawBufferSocket)              │
│ - Encryption/decryption for TLS                            │
│ - Pass-through for raw TCP                                 │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ read() / write()
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│ IoSocketHandleImpl (wraps file descriptor)                 │
└─────────────────────────────────────────────────────────────┘
                            ▲ │
                            │ │ readv() / writev() syscalls
                            │ ▼
┌─────────────────────────────────────────────────────────────┐
│ Kernel (TCP/IP stack, socket buffers)                      │
└─────────────────────────────────────────────────────────────┘
```

---

## Related Documentation

- [source_common_network_README.md](source_common_network_README.md) - Network layer overview
- [connection_impl.md](connection_impl.md) - ConnectionImpl class details
- [filter_manager_impl.md](filter_manager_impl.md) - Filter manager internals
- [listeners.md](listeners.md) - Listener implementations
- [socket_and_io_handle.md](socket_and_io_handle.md) - Socket and IO handle details
