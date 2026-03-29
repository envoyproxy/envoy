# Data Movement and Ownership in HTTP Layers

**Purpose**: Understanding how data flows through Envoy's HTTP stack using move semantics, zero-copy patterns, and ownership transfer

## Table of Contents

- [Overview](#overview)
- [Core Principles](#core-principles)
- [Smart Pointer Patterns](#smart-pointer-patterns)
- [Header Map Transfers](#header-map-transfers)
- [Buffer Data Movement](#buffer-data-movement)
- [Message Ownership](#message-ownership)
- [Filter Chain Data Flow](#filter-chain-data-flow)
- [Codec Layer Transfers](#codec-layer-transfers)
- [Connection Pool Ownership](#connection-pool-ownership)
- [Performance Implications](#performance-implications)
- [Common Patterns](#common-patterns)
- [Anti-Patterns](#anti-patterns)

---

## Overview

Envoy's HTTP stack is designed for **zero-copy** and **efficient ownership transfer**. Data moves through layers using:

1. **Move semantics** (`std::move`) - Transfer ownership without copying
2. **Smart pointers** (`std::unique_ptr`, `std::shared_ptr`) - Clear ownership semantics
3. **References** - Zero-copy access
4. **Rvalue references** (`&&`) - Explicit ownership transfer

### Why This Matters

**Bad (copying)**:
```cpp
void processHeaders(RequestHeaderMap headers) {  // Copy!
    // Process headers
}
// Every call copies entire header map (~1KB+)
```

**Good (move/reference)**:
```cpp
void processHeaders(RequestHeaderMapPtr&& headers) {  // Move!
    // Process headers - no copy
}
// Ownership transferred, zero copies
```

---

## Core Principles

### 1. Ownership Transfer = Move Semantics

```cpp
// unique_ptr with && = clear ownership transfer
void decodeHeaders(RequestHeaderMapPtr&& headers, bool end_stream) {
    // Caller gives up ownership, this function now owns headers
    stored_headers_ = std::move(headers);  // Transfer to member
}
```

### 2. Shared Access = References

```cpp
// & = no ownership transfer, just access
FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    // Can read/modify headers, but don't own them
    headers.addCopy(LowerCaseString("x-custom"), "value");
    return FilterHeadersStatus::Continue;
}
```

### 3. Optional Ownership = shared_ptr

```cpp
// shared_ptr when multiple components need access
using HostDescriptionConstSharedPtr = std::shared_ptr<const HostDescription>;

void onPoolReady(
    RequestEncoder& encoder,
    HostDescriptionConstSharedPtr host  // Shared ownership
) {
    // Both caller and this function can keep reference
    stored_host_ = host;  // Copy shared_ptr, not the object
}
```

---

## Smart Pointer Patterns

### unique_ptr - Exclusive Ownership

**When to use**: Single owner, transfer ownership between layers

```cpp
// Type definitions
using RequestHeaderMapPtr = std::unique_ptr<RequestHeaderMap>;
using ResponseHeaderMapPtr = std::unique_ptr<ResponseHeaderMap>;
using RequestMessagePtr = std::unique_ptr<RequestMessage>;
using ResponseMessagePtr = std::unique_ptr<ResponseMessage>;

// Factory creates and transfers ownership
RequestHeaderMapPtr createRequestHeaders() {
    auto headers = RequestHeaderMapImpl::create();  // unique_ptr
    headers->setMethod("GET");
    headers->setPath("/api");
    return headers;  // Move on return
}

// Caller receives ownership
auto headers = createRequestHeaders();
// headers is moved, createRequestHeaders no longer has access
```

### Ownership Transfer Chain

```cpp
// 1. Create headers
auto headers = RequestHeaderMapImpl::create();  // unique_ptr

// 2. Transfer to decoder (takes &&)
decoder->decodeHeaders(std::move(headers), true);
// headers is now nullptr, decoder owns it

// 3. Decoder stores it
void RequestDecoderImpl::decodeHeaders(RequestHeaderMapPtr&& headers, bool end_stream) {
    // Take ownership
    request_headers_ = std::move(headers);

    // Pass to connection manager (by reference, no ownership transfer)
    connection_callbacks_.onHeaders(*request_headers_, end_stream);
}

// 4. Connection manager processes (no ownership)
void ConnectionManagerImpl::onHeaders(RequestHeaderMap& headers, bool end_stream) {
    // Just access, don't store
    auto method = headers.getMethodValue();

    // Pass to filter chain (by reference)
    filter_manager_.decodeHeaders(headers, end_stream);
}
```

### shared_ptr - Shared Ownership

**When to use**: Multiple components need long-lived access

```cpp
// Host description shared across components
using HostDescriptionConstSharedPtr = std::shared_ptr<const HostDescription>;

class ConnectionPool {
    HostDescriptionConstSharedPtr host_;  // Shared ownership
};

class ActiveConnection {
    HostDescriptionConstSharedPtr host_;  // Shared ownership
};

// Both keep the host alive
// Destroyed only when both release their shared_ptr
```

### Factory Pattern

```cpp
// Factory returns unique_ptr
class HeaderMapFactory {
public:
    static RequestHeaderMapPtr createRequestHeaderMap() {
        return std::make_unique<RequestHeaderMapImpl>();
    }
};

// Usage
auto headers = HeaderMapFactory::createRequestHeaderMap();
headers->setMethod("POST");
processRequest(std::move(headers));  // Transfer ownership
```

---

## Header Map Transfers

### Decoder Path (Request Headers)

```cpp
// HTTP/2 Codec → Request Decoder
void Http2Stream::decodeHeaders(Http2HeaderBlock header_block) {
    // 1. Parse headers into header map
    auto headers = RequestHeaderMapImpl::create();
    for (auto& header : header_block) {
        headers->addCopy(LowerCaseString(header.first), header.second);
    }

    // 2. Transfer to request decoder (move)
    request_decoder_.decodeHeaders(std::move(headers), end_stream);
}

// Request Decoder → Connection Manager
void RequestDecoderImpl::decodeHeaders(
    RequestHeaderMapPtr&& headers,  // Take ownership via &&
    bool end_stream
) {
    // 3. Store headers
    request_headers_ = std::move(headers);

    // 4. Pass by reference to connection manager (no ownership transfer)
    connection_callbacks_.decodeHeaders(*request_headers_, end_stream);
}

// Connection Manager → Filter Chain
void ConnectionManagerImpl::decodeHeaders(
    RequestHeaderMap& headers,  // Just reference, no ownership
    bool end_stream
) {
    // 5. Pass by reference through filter chain
    for (auto& filter : filters_) {
        auto status = filter->decodeHeaders(headers, end_stream);
        if (status == FilterHeadersStatus::StopIteration) {
            break;
        }
    }
}

// Filter processes (no ownership)
FilterHeadersStatus MyFilter::decodeHeaders(
    RequestHeaderMap& headers,  // Mutable reference
    bool end_stream
) {
    // Can modify headers
    headers.addCopy(LowerCaseString("x-custom"), "value");

    // But don't own them
    return FilterHeadersStatus::Continue;
}
```

### Encoder Path (Response Headers)

```cpp
// Router creates response headers
void RouterFilter::onUpstreamHeaders(ResponseHeaderMapPtr&& headers) {
    // 1. Receive ownership from upstream
    response_headers_ = std::move(headers);

    // 2. Pass by reference to encoder filter chain
    encodeHeaders(*response_headers_, end_stream);
}

// Filter chain processes by reference
FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
    // Filters modify headers in place
    for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) {
        auto status = (*it)->encodeHeaders(headers, end_stream);
        if (status != FilterHeadersStatus::Continue) {
            break;
        }
    }
}

// Response Encoder gets reference
void ResponseEncoderImpl::encodeHeaders(
    const ResponseHeaderMap& headers,  // Const reference (read-only)
    bool end_stream
) {
    // 3. Encode to wire format
    codec_->encodeHeaders(headers, end_stream);
}
```

---

## Buffer Data Movement

### Buffer Reference Pattern

Buffers are **never** moved/copied - always passed by reference:

```cpp
// StreamEncoder interface
class StreamEncoder {
public:
    // Buffer passed by reference, NOT moved
    virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;
    //                       ^^^^^^^^^^^^^^^^^
    //                       Reference - no ownership transfer
};

// Why? Buffers may be:
// 1. Very large (MBs)
// 2. Shared across components
// 3. Drained incrementally
```

### Buffer Draining (Zero-Copy)

```cpp
void processData(Buffer::Instance& source_buffer) {
    // Read without copying
    auto length = source_buffer.length();
    auto data = source_buffer.frontSlice();

    // Process data...

    // Drain (remove processed data)
    source_buffer.drain(length);  // Zero-copy removal
}

// Move buffer contents (still zero-copy)
void transferBuffer(Buffer::Instance& source, Buffer::Instance& dest) {
    dest.move(source);  // Move contents, no copy
    // source is now empty, dest has all data
}
```

### Filter Data Flow

```cpp
// Decode data path
FilterDataStatus MyFilter::decodeData(
    Buffer::Instance& data,  // Reference to shared buffer
    bool end_stream
) {
    // Option 1: Pass through unchanged
    return FilterDataStatus::Continue;

    // Option 2: Inspect data (zero-copy)
    auto slice = data.frontSlice();
    if (containsMalicious(slice)) {
        return FilterDataStatus::StopIterationNoBuffer;
    }

    // Option 3: Modify data in place
    data.prepend("prefix");
    data.add("suffix");

    // Option 4: Buffer for later
    decoder_callbacks_->addDecodedData(data, false);
    return FilterDataStatus::StopIterationAndBuffer;
}
```

### Buffer Addition vs. Injection

```cpp
// Add data to existing buffer
void addDecodedData(Buffer::Instance& data, bool streaming) {
    // Appends to buffered data
    buffered_data_.move(data);  // Zero-copy append
}

// Inject new data into filter chain
void injectDecodedDataToFilterChain(Buffer::Instance& data, bool end_stream) {
    // Creates new data event in filter chain
    filter_manager_.injectDecodedDataToFilterChain(data, end_stream);
}
```

---

## Message Ownership

### Request Message Lifecycle

```cpp
// 1. Create message (unique ownership)
auto request = std::make_unique<RequestMessageImpl>();
request->headers().setMethod("POST");
request->headers().setPath("/api/endpoint");
request->body().add("request body");

// 2. Transfer to async client
auto* handle = async_client_->send(
    std::move(request),  // Transfer ownership
    *this,               // Callbacks
    options
);
// request is nullptr here, async client owns it

// 3. Async client processes
void AsyncClientImpl::send(
    RequestMessagePtr&& request,  // Take ownership
    Callbacks& callbacks,
    const RequestOptions& options
) {
    // Store request
    active_request_->request_ = std::move(request);

    // Extract headers by reference
    auto& headers = active_request_->request_->headers();

    // Start request
    encoder_->encodeHeaders(headers, !has_body);

    if (has_body) {
        encoder_->encodeData(active_request_->request_->body(), true);
    }
}
```

### Response Message Lifecycle

```cpp
// 1. Codec creates response
void ResponseDecoderImpl::decodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
    // Create response message
    response_message_ = std::make_unique<ResponseMessageImpl>(std::move(headers));
}

void ResponseDecoderImpl::decodeData(Buffer::Instance& data, bool end_stream) {
    // Add data to response body
    response_message_->body().move(data);

    if (end_stream) {
        // 2. Transfer to callbacks
        callbacks_->onSuccess(*request_, std::move(response_message_));
        // response_message_ is nullptr now
    }
}

// 3. Caller receives ownership
void MyFilter::onSuccess(
    const Request& request,
    ResponseMessagePtr&& response  // Receive ownership
) {
    // Store or process
    stored_response_ = std::move(response);

    // Extract data
    auto status = stored_response_->headers().getStatusValue();
    auto body = stored_response_->bodyAsString();
}
```

### Trailers Transfer

```cpp
// Set trailers (transfer ownership)
void Message::trailers(std::unique_ptr<TrailerType>&& trailers) {
    trailers_ = std::move(trailers);
}

// Get trailers (no ownership transfer)
TrailerType* Message::trailers() {
    return trailers_.get();  // Raw pointer, still owned by Message
}

// Usage
auto trailers = RequestTrailerMapImpl::create();
trailers->addCopy(LowerCaseString("x-trailer"), "value");
request->trailers(std::move(trailers));  // Transfer to request
```

---

## Filter Chain Data Flow

### Headers Through Filter Chain

```cpp
// Connection Manager orchestrates
void FilterManager::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    // Headers passed by reference through all filters
    for (auto& filter : decoder_filters_) {
        FilterHeadersStatus status = filter->decodeHeaders(headers, end_stream);

        if (status == FilterHeadersStatus::StopIteration) {
            // Filter wants to process async
            paused_ = true;
            return;
        }
    }

    // All filters processed, send to router
    router_filter_->decodeHeaders(headers, end_stream);
}

// Filter 1 modifies headers
FilterHeadersStatus Filter1::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    headers.addCopy(LowerCaseString("x-filter-1"), "processed");
    return FilterHeadersStatus::Continue;
}

// Filter 2 sees modifications
FilterHeadersStatus Filter2::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    // Can see "x-filter-1" header added by Filter1
    auto filter1_header = headers.get(LowerCaseString("x-filter-1"));

    headers.addCopy(LowerCaseString("x-filter-2"), "processed");
    return FilterHeadersStatus::Continue;
}

// Router sees all modifications
FilterHeadersStatus RouterFilter::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
    // Headers have both "x-filter-1" and "x-filter-2"
    // Send upstream with all modifications
}
```

### Data Through Filter Chain

```cpp
// Shared buffer passed by reference
void FilterManager::decodeData(Buffer::Instance& data, bool end_stream) {
    for (auto& filter : decoder_filters_) {
        FilterDataStatus status = filter->decodeData(data, end_stream);

        if (status == FilterDataStatus::StopIterationAndBuffer) {
            // Buffer data for this filter
            buffered_data_.move(data);  // Move to buffer, data is now empty
            return;
        }

        // data may be modified by filter
    }
}

// Filter modifies data in place
FilterDataStatus CompressionFilter::decodeData(Buffer::Instance& data, bool end_stream) {
    // Decompress in place
    Buffer::OwnedImpl decompressed;
    decompress(data, decompressed);

    // Replace data
    data.drain(data.length());     // Empty original
    data.move(decompressed);       // Move decompressed data

    return FilterDataStatus::Continue;
}
```

---

## Codec Layer Transfers

### Downstream (Client → Envoy)

```cpp
// 1. Network connection receives bytes
void ConnectionImpl::onRead(Buffer::Instance& data) {
    // Pass to codec for parsing
    codec_->dispatch(data);  // data passed by reference
}

// 2. HTTP/2 codec parses frames
Status Http2ConnectionImpl::dispatch(Buffer::Instance& data) {
    // Parse frames from buffer
    while (data.length() > 0) {
        auto frame = parseFrame(data);

        if (frame.type == HEADERS) {
            // Create header map
            auto headers = RequestHeaderMapImpl::create();
            decodeHeaders(frame, *headers);

            // Transfer to stream decoder
            stream->decoder().decodeHeaders(std::move(headers), frame.end_stream);
        }
        else if (frame.type == DATA) {
            // Create buffer slice (zero-copy view)
            Buffer::OwnedImpl frame_data;
            data.copyOut(0, frame.length, frame_data);
            data.drain(frame.length);

            // Pass to stream decoder by reference
            stream->decoder().decodeData(frame_data, frame.end_stream);
        }
    }
}

// 3. Stream decoder receives data
void RequestDecoderImpl::decodeHeaders(
    RequestHeaderMapPtr&& headers,
    bool end_stream
) {
    // Store and forward
    request_headers_ = std::move(headers);
    callbacks_.decodeHeaders(*request_headers_, end_stream);
}
```

### Upstream (Envoy → Server)

```cpp
// 1. Router sends request upstream
void RouterFilter::sendRequestUpstream() {
    // Create encoder callbacks
    upstream_request_ = std::make_unique<UpstreamRequest>(*this);

    // Get connection from pool
    conn_pool_->newStream(
        *upstream_request_,  // Response decoder
        *this,               // Connection callbacks
        options
    );
}

// 2. Connection pool provides encoder
void onPoolReady(
    RequestEncoder& encoder,  // Reference to encoder
    HostDescriptionConstSharedPtr host,
    StreamInfo::StreamInfo& info,
    absl::optional<Http::Protocol> protocol
) {
    // Store encoder (no ownership)
    upstream_request_->encoder_ = &encoder;

    // Send headers (by reference)
    encoder.encodeHeaders(request_headers_, !has_body);

    if (has_body) {
        // Send body (by reference)
        encoder.encodeData(request_body_, true);
    }
}

// 3. Encoder sends to codec
void RequestEncoderImpl::encodeHeaders(
    const RequestHeaderMap& headers,  // Const reference
    bool end_stream
) {
    // Codec encodes to wire format
    codec_->encodeHeaders(headers, end_stream);
}

// 4. Codec writes to connection
void Http2Codec::encodeHeaders(
    const RequestHeaderMap& headers,
    bool end_stream
) {
    // Serialize headers to HPACK
    Buffer::OwnedImpl header_block;
    hpack_encoder_.encode(headers, header_block);

    // Write HEADERS frame
    connection_->write(header_block, end_stream);
}
```

---

## Connection Pool Ownership

### Creating Connections

```cpp
// Pool creates active client
ActiveClientPtr HttpConnPoolImpl::instantiateActiveClient() {
    // Get connection data from host
    auto data = host_->createConnection(dispatcher_, options_);

    // Create codec client (takes ownership of connection)
    auto codec_client = createCodecClient(std::move(data));

    // Create active client (takes ownership of codec client)
    auto active_client = std::make_unique<ActiveClient>(
        *this,
        std::move(codec_client)
    );

    return active_client;  // Transfer ownership to pool
}

// Pool stores active clients
void HttpConnPoolImpl::addActiveClient(ActiveClientPtr&& client) {
    active_clients_.push_back(std::move(client));
}
```

### Stream Assignment

```cpp
// Pending stream waiting for connection
struct HttpPendingStream {
    HttpPendingStream(
        Http::ResponseDecoder& decoder,
        Http::ConnectionPool::Callbacks& callbacks
    ) : decoder_(&decoder), callbacks_(&callbacks) {}

    Http::ResponseDecoder* decoder_;  // Raw pointer, doesn't own
    Http::ConnectionPool::Callbacks* callbacks_;  // Raw pointer
};

// Attach pending stream to active client
void HttpConnPoolImpl::attachStreamToClient(
    ActiveClient& client,
    PendingStream& pending
) {
    auto& context = static_cast<HttpAttachContext&>(pending.context());

    // Create encoder (client still owns connection)
    auto& encoder = client.newStreamEncoder(*context.decoder_);

    // Notify callbacks (pass encoder by reference)
    context.callbacks_->onPoolReady(
        encoder,    // Reference
        host_,      // Shared pointer (shared ownership)
        client.streamInfo(),
        client.protocol()
    );
}
```

---

## Performance Implications

### Copy vs. Move Benchmarks

**Scenario**: Processing 1000 requests with headers

**With Copies**:
```cpp
void processRequest(RequestHeaderMap headers) {  // Copy constructor
    // Process headers
}

// Cost:
// - 1000 header map copies (~1KB each)
// - ~1MB memory allocated
// - ~10ms CPU time
```

**With Moves**:
```cpp
void processRequest(RequestHeaderMapPtr&& headers) {  // Move
    // Process headers
}

// Cost:
// - 1000 pointer transfers (~8 bytes each)
// - ~8KB memory operations
// - ~0.1ms CPU time
```

**Performance Gain**: **100x faster**, **99% less memory**

### Buffer Zero-Copy

**Bad (copying)**:
```cpp
void transformData(const Buffer::Instance& input, Buffer::Instance& output) {
    std::string temp = input.toString();  // Copy to string
    transform(temp);                       // Transform
    output.add(temp);                      // Copy back to buffer
}
// 2 full copies of data
```

**Good (zero-copy)**:
```cpp
void transformData(Buffer::Instance& data) {
    // Transform in place using buffer slices
    for (const auto& slice : data.getRawSlices()) {
        transformInPlace(slice.mem_, slice.len_);
    }
}
// Zero copies
```

### Reference vs. Pointer

**Reference (preferred)**:
```cpp
void processHeaders(RequestHeaderMap& headers) {
    // Direct access, no null check needed
    headers.addCopy(LowerCaseString("x-custom"), "value");
}
```

**Pointer (less preferred)**:
```cpp
void processHeaders(RequestHeaderMap* headers) {
    // Need null check
    if (headers != nullptr) {
        headers->addCopy(LowerCaseString("x-custom"), "value");
    }
}
```

**When to use pointer**: When nullptr is a valid state (optional)

---

## Common Patterns

### Pattern 1: Factory → Ownership Transfer

```cpp
// Factory creates with unique_ptr
class Factory {
public:
    static RequestHeaderMapPtr create() {
        return std::make_unique<RequestHeaderMapImpl>();
    }
};

// Caller receives ownership
auto headers = Factory::create();
headers->setMethod("GET");

// Transfer to consumer
consumer.setHeaders(std::move(headers));
```

### Pattern 2: Layer Boundary Transfer

```cpp
// Codec layer creates and transfers to application layer
void CodecImpl::onHeaders(HeaderBlock header_block) {
    auto headers = RequestHeaderMapImpl::create();
    parseHeaders(header_block, *headers);

    // Transfer across layer boundary
    decoder_->decodeHeaders(std::move(headers), end_stream);
}

// Application layer receives and stores
void DecoderImpl::decodeHeaders(RequestHeaderMapPtr&& headers, bool end_stream) {
    headers_ = std::move(headers);  // Now owned by application layer
}
```

### Pattern 3: Shared Access via Reference

```cpp
// Filter chain - all filters get reference
void FilterChain::processHeaders(RequestHeaderMap& headers) {
    for (auto& filter : filters_) {
        filter->decodeHeaders(headers, end_stream);
        // All filters see same header map
        // No copies, no ownership transfer
    }
}
```

### Pattern 4: Optional Ownership with shared_ptr

```cpp
// Multiple components share access
class Stream {
    RouteConstSharedPtr route_;  // Shared ownership
};

class Filter {
    RouteConstSharedPtr route_;  // Shared ownership
};

// Both keep route alive until they're done
void setRoute(RouteConstSharedPtr route) {
    route_ = route;  // Copy shared_ptr, not the route object
}
```

### Pattern 5: Callback with Context

```cpp
// Store context that outlives async operation
void makeAsyncCall() {
    auto context = std::make_shared<CallContext>();
    context->request_id = generateId();

    async_client_->send(
        createRequest(),
        [context](ResponseMessagePtr&& response) {
            // Capture shared_ptr to keep context alive
            processResponse(*context, std::move(response));
        },
        options
    );
}
```

---

## Anti-Patterns

### ❌ Anti-Pattern 1: Unnecessary Copy

```cpp
// BAD: Copy entire header map
void processHeaders(RequestHeaderMap headers) {  // Copy!
    auto method = headers.getMethodValue();
}

// GOOD: Use reference
void processHeaders(const RequestHeaderMap& headers) {  // No copy
    auto method = headers.getMethodValue();
}
```

### ❌ Anti-Pattern 2: Dangling Reference

```cpp
// BAD: Returning reference to temporary
const RequestHeaderMap& getHeaders() {
    auto headers = RequestHeaderMapImpl::create();
    return *headers;  // Dangling! headers destroyed
}

// GOOD: Return ownership
RequestHeaderMapPtr getHeaders() {
    return RequestHeaderMapImpl::create();  // Transfer ownership
}
```

### ❌ Anti-Pattern 3: Use After Move

```cpp
// BAD: Using moved-from object
auto headers = createHeaders();
consumer.process(std::move(headers));
headers->setMethod("GET");  // BUG! headers is nullptr

// GOOD: Don't use after move
auto headers = createHeaders();
consumer.process(std::move(headers));
// Don't touch headers anymore
```

### ❌ Anti-Pattern 4: Copying shared_ptr Contents

```cpp
// BAD: Copying shared object
void storeHost(HostDescriptionConstSharedPtr host) {
    // Don't do this:
    stored_host_copy_ = std::make_shared<HostDescription>(*host);  // Copy!
}

// GOOD: Share the pointer
void storeHost(HostDescriptionConstSharedPtr host) {
    stored_host_ = host;  // Share pointer, no copy
}
```

### ❌ Anti-Pattern 5: Storing Reference to Temporary

```cpp
// BAD: Storing reference that might outlive object
class MyFilter {
    const RequestHeaderMap& headers_;  // Dangerous!

    MyFilter(const RequestHeaderMap& headers) : headers_(headers) {}
    // If headers is destroyed, headers_ is dangling
};

// GOOD: Store by value or pointer with clear ownership
class MyFilter {
    RequestHeaderMapPtr headers_;  // Own it

    MyFilter(RequestHeaderMapPtr headers) : headers_(std::move(headers)) {}
};
```

### ❌ Anti-Pattern 6: Double Move

```cpp
// BAD: Moving same object twice
auto headers = createHeaders();
consumer1.process(std::move(headers));  // First move - OK
consumer2.process(std::move(headers));  // Second move - BUG! headers is nullptr

// GOOD: Move only once
auto headers = createHeaders();
consumer.process(std::move(headers));
// Done with headers
```

### ❌ Anti-Pattern 7: Converting unique_ptr to Raw Pointer Too Early

```cpp
// BAD: Losing ownership tracking
auto headers = createHeaders();
auto* raw_ptr = headers.release();  // Lost unique_ptr tracking
processHeaders(raw_ptr);
// Who deletes raw_ptr? Memory leak!

// GOOD: Keep unique_ptr until transfer
auto headers = createHeaders();
processHeaders(std::move(headers));  // Clear ownership transfer
```

---

## Summary

### Key Takeaways

1. **Move, Don't Copy**: Use `std::move` and rvalue references (`&&`) for ownership transfer
2. **Reference for Shared Access**: Use `&` when multiple components need access without ownership
3. **unique_ptr for Exclusive Ownership**: Clear single-owner semantics
4. **shared_ptr for Shared Ownership**: When multiple components need long-lived access
5. **Buffers by Reference**: Never move/copy buffers, always pass by reference
6. **Zero-Copy Philosophy**: Minimize data copies, use references and moves

### Decision Tree

```
Need to transfer data?
│
├─ Headers/Messages?
│  ├─ Creating new? → unique_ptr, transfer with std::move
│  ├─ Processing only? → const reference (&)
│  └─ Modifying? → mutable reference (&)
│
├─ Buffers?
│  └─ Always use reference (Buffer::Instance&)
│
├─ Host/Route Info?
│  ├─ Multiple owners needed? → shared_ptr
│  └─ Single owner? → unique_ptr with std::move
│
└─ Simple values (int, bool, etc.)?
   └─ Copy (cheap)
```

### Performance Checklist

- [ ] Are headers passed by `&&` or `&`, never by value?
- [ ] Are buffers always passed by `Buffer::Instance&`?
- [ ] Are unique_ptrs moved with `std::move`, not copied?
- [ ] Are shared_ptrs used only when multiple owners needed?
- [ ] Are moved-from variables not used after `std::move`?
- [ ] Are temporary objects not stored by reference?
- [ ] Is raw pointer ownership clear (prefer unique_ptr)?

### Common Signatures

```cpp
// Creating/transferring ownership
RequestHeaderMapPtr createHeaders();
void setHeaders(RequestHeaderMapPtr&& headers);

// Shared access (read-only)
void processHeaders(const RequestHeaderMap& headers);

// Shared access (modifiable)
void modifyHeaders(RequestHeaderMap& headers);

// Buffer operations
void encodeData(Buffer::Instance& data, bool end_stream);

// Shared ownership
void setRoute(RouteConstSharedPtr route);

// Callbacks with responses
void onSuccess(const Request& request, ResponseMessagePtr&& response);
```

**Remember**: In Envoy's HTTP stack, **data moves efficiently** through careful use of move semantics, smart pointers, and references. Understanding these patterns is key to writing performant Envoy code.
