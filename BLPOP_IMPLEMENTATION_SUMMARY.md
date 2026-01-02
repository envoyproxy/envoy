# BLPOP Implementation Summary

## Overview
This document describes the implementation of Redis BLPOP (Blocking List Pop) command support in Envoy's Redis proxy filter with proper timeout handling.

## Problem Statement
BLPOP is a blocking command that can have timeouts (e.g., 6 seconds) that exceed Envoy's configured `op_timeout` (e.g., 5 seconds). Without special handling, this causes "upstream failure" errors when the operation timeout fires before the BLPOP timeout.

**Example of the problem:**
```bash
# With op_timeout: 5s
127.0.0.1:1999> BLPOP mylist 6
(error) upstream failure(5.00s)  # Envoy timed out at 5s, BLPOP wanted 6s
```

## Solution
The implementation uses an `is_blocking_client` flag (similar to the existing `is_transaction_client` flag) to bypass operation timeouts for blocking commands like BLPOP. This allows BLPOP to have timeouts longer than Envoy's `op_timeout` without requiring configuration changes.

## Architecture Overview

### The `is_blocking_client` Flag Pattern

The solution follows the same pattern as transaction handling in Envoy:
- A boolean flag is passed through the entire request chain
- The flag is set on the client connection BEFORE making the request
- Three timeout check locations verify `!is_blocking_client_` before enabling timeouts
- The flag automatically resets when all requests complete

### Request Flow Diagram

```
Client Request: BLPOP mylist 6
        ↓
BlockingRequest::create()
  • Validates syntax (min 3 args)
  • Validates timeout parameter (≥ 0)
  • Passes is_blocking_command=true
        ↓
makeSingleServerRequest(..., is_blocking_command=true)
  • Routes to appropriate upstream
  • Passes flag to connection pool
        ↓
route->upstream()->makeRequest(..., is_blocking_command=true)
        ↓
InstanceImpl::makeRequest(..., is_blocking_command=true)
        ↓
ThreadLocalPool::makeRequest(..., is_blocking_command=true)
        ↓
ThreadLocalPool::makeRequestToHost(..., is_blocking_command=true)
  • Calls client->setBlockingClient(true) ← FLAG SET HERE
  • Calls client->makeRequest()
        ↓
ClientImpl::makeRequest()
  • Check #1: if (!is_blocking_client_) enableTimer()
  • SKIPS timeout because is_blocking_client_ = true ✓
        ↓
Request sent to Redis, waits up to 6 seconds
        ↓
ClientImpl::onRespValue()
  • Response received from Redis
  • Check #3: if (!is_blocking_client_) enableTimer()
  • Resets is_blocking_client_ = false when done ✓
        ↓
Response returned to client
```

## Implementation Details

### 1. Command Registration

**Files:** `supported_commands.h`, `supported_commands.cc`

**Changes:**
- Added `blockingCommands()` function returning set with "blpop"
- Added `isBlockingCommand()` helper method
- Added "blpop" to `writeCommands()` set (BLPOP modifies list state)
- Updated `isSupportedCommand()` to check `blockingCommands()`

```cpp
static const absl::flat_hash_set<std::string>& blockingCommands() {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, "blpop");
}

static bool isBlockingCommand(const std::string& command) {
  return blockingCommands().contains(command);
}
```

### 2. Client Interface

**File:** `client.h`

**Changes:**
- Added `setBlockingClient(bool is_blocking)` pure virtual method

```cpp
/**
 * Mark this client as a blocking client (for commands like BLPOP).
 * Blocking clients do not have operation timeouts enabled.
 * @param is_blocking true if this client is for blocking commands.
 */
virtual void setBlockingClient(bool is_blocking) PURE;
```

### 3. Client Implementation - The Core Logic

**Files:** `client_impl.h`, `client_impl.cc`

**Changes:**

#### Member Variable:
```cpp
bool is_blocking_client_;  // Initialized to false in constructor
```

#### Method Implementation:
```cpp
void setBlockingClient(bool is_blocking) override { 
  is_blocking_client_ = is_blocking; 
}
```

#### Three Critical Timeout Checks:

**Check #1: In `makeRequest()` - When making a request**
```cpp
// Only enable op timeout if:
// - Connected and first request in pipeline
// - NOT a blocking client
if (connected_ && pending_requests_.size() == 1 && !is_blocking_client_) {
  connect_or_op_timer_->enableTimer(config_->opTimeout());
}
```

**Check #2: In `onEvent()` - When connection is established**
```cpp
if (event == Network::ConnectionEvent::Connected) {
  connected_ = true;
  ASSERT(!pending_requests_.empty());
  // Only enable op timeout for non-blocking clients
  if (!is_blocking_client_) {
    connect_or_op_timer_->enableTimer(config_->opTimeout());
  }
}
```

**Check #3: In `onRespValue()` - After receiving response**
```cpp
// Re-enable timer for next request, but not for blocking clients
if (pending_requests_.empty()) {
  connect_or_op_timer_->disableTimer();
  // Reset blocking client flag when all requests are complete
  is_blocking_client_ = false;  // AUTO-RESET
} else if (!is_blocking_client_) {
  connect_or_op_timer_->enableTimer(config_->opTimeout());
}
```

**Why three checks are needed:**
1. **makeRequest()**: Prevents timeout when initiating the blocking request
2. **onEvent()**: Prevents timeout when connection is established for blocking request
3. **onRespValue()**: Prevents timeout when waiting for more responses AND auto-resets the flag

**Auto-reset mechanism:**
When all pending requests complete (`pending_requests_.empty()`), the flag is automatically reset to `false`. This allows the same connection to handle both blocking and non-blocking commands without state leakage.

### 4. Connection Pool Interface

**File:** `conn_pool.h`

**Changes:**
- Added `is_blocking_command` parameter (default `false`) to `makeRequest()`
- Added `is_blocking_command` parameter (default `false`) to `makeRequestToShard()`

```cpp
virtual Common::Redis::Client::PoolRequest*
makeRequest(const std::string& hash_key, RespVariant&& request, 
            PoolCallbacks& callbacks,
            Common::Redis::Client::Transaction& transaction,
            bool is_blocking_command = false) PURE;
```

### 5. Connection Pool Implementation

**Files:** `conn_pool_impl.h`, `conn_pool_impl.cc`

**Changes:**
- Added `is_blocking_command` parameter to all pool methods
- Updated `makeRequestToHost()` to call `setBlockingClient()` before making request

**Key method - `makeRequestToHost()`:**
```cpp
Common::Redis::Client::PoolRequest*
InstanceImpl::ThreadLocalPool::makeRequestToHost(
    Upstream::HostConstSharedPtr& host,
    RespVariant&& request, PoolCallbacks& callbacks,
    Common::Redis::Client::Transaction& transaction,
    bool is_blocking_command) {
  
  // ... existing code ...
  
  if (!transaction.active_) {
    ThreadLocalActiveClientPtr& client = this->threadLocalActiveClient(host);
    
    // Set blocking client flag BEFORE making the request
    if (is_blocking_command) {
      client->redis_client_->setBlockingClient(true);
    }
    
    pending_request.request_handler_ = client->redis_client_->makeRequest(
        getRequest(pending_request.incoming_request_), pending_request);
  } else {
    // Also handle transaction clients
    if (is_blocking_command) {
      transaction.clients_[client_idx]->setBlockingClient(true);
    }
    
    pending_request.request_handler_ = transaction.clients_[client_idx]->makeRequest(
        getRequest(pending_request.incoming_request_), pending_request);
  }
}
```

**Critical detail:** The flag MUST be set BEFORE calling `makeRequest()` so that the timeout checks in `ClientImpl::makeRequest()` see the correct flag value.

### 6. Command Splitter

**Files:** `command_splitter_impl.h`, `command_splitter_impl.cc`

**Changes:**

#### New `BlockingRequest` class:
```cpp
/**
 * BlockingRequest handles blocking Redis commands like BLPOP, BRPOP, etc.
 * These commands have a timeout parameter (in seconds) as the last argument.
 */
class BlockingRequest : public SingleServerRequest {
public:
  static SplitRequestPtr create(Router& router, 
                                Common::Redis::RespValuePtr&& incoming_request,
                                SplitCallbacks& callbacks, 
                                CommandStats& command_stats,
                                TimeSource& time_source, 
                                bool delay_command_latency,
                                const StreamInfo::StreamInfo& stream_info);
};
```

#### Command routing logic:
```cpp
// In InstanceImpl::makeRequest()
if (Common::Redis::SupportedCommands::isBlockingCommand(command_name)) {
  request_ptr = BlockingRequest::create(...);
} else {
  request_ptr = handler->handler_.get().startRequest(...);
}
```

**Why this approach:**
- Uses `SupportedCommands::isBlockingCommand()` instead of hardcoding "blpop"
- Automatically handles any command added to `blockingCommands()` set
- When BRPOP, BRPOPLPUSH, etc. are added to the set, they'll automatically use `BlockingRequest`
- No code changes needed in command_splitter when adding new blocking commands

#### `BlockingRequest::create()` implementation:
```cpp
SplitRequestPtr BlockingRequest::create(...) {
  // Validate minimum arguments: BLPOP key timeout (3 args)
  if (incoming_request->asArray().size() < 3) {
    onWrongNumberOfArguments(callbacks, *incoming_request);
    command_stats.error_.inc();
    return nullptr;
  }

  // Extract and validate timeout parameter (last argument)
  const std::string& timeout_str = incoming_request->asArray().back().asString();
  double timeout_seconds = 0;

  if (!absl::SimpleAtod(timeout_str, &timeout_seconds) || timeout_seconds < 0) {
    callbacks.onResponse(Common::Redis::Utility::makeError(
        "ERR timeout is not a float or out of range"));
    command_stats.error_.inc();
    return nullptr;
  }

  // Create request and pass is_blocking_command=true
  request_ptr->handle_ = makeSingleServerRequest(
      route, command, key, base_request, *request_ptr, 
      callbacks.transaction(), 
      true);  // is_blocking_command=true
}
```

#### Updated `makeSingleServerRequest()`:
```cpp
Common::Redis::Client::PoolRequest* makeSingleServerRequest(
    const RouteSharedPtr& route, const std::string& command, 
    const std::string& key,
    Common::Redis::RespValueConstSharedPtr incoming_request, 
    ConnPool::PoolCallbacks& callbacks,
    Common::Redis::Client::Transaction& transaction, 
    bool is_blocking_command = false) {
  
  // Pass the flag through to the connection pool
  auto handler = route->upstream(command)->makeRequest(
      key, ConnPool::RespVariant(incoming_request),
      callbacks, transaction, is_blocking_command);
  
  // Mirror requests always use is_blocking_command=false
  if (handler) {
    for (auto& mirror_policy : route->mirrorPolicies()) {
      mirror_policy->upstream()->makeRequest(
          key, ConnPool::RespVariant(incoming_request),
          null_pool_callbacks, transaction, false);
    }
  }
  return handler;
}
```

## Key Design Decisions

### 1. Per-Request Flag, Not Per-Connection
The `is_blocking_command` flag is passed through the entire request chain and set per-request. This allows the same client connection to handle both blocking and non-blocking commands without interference.

### 2. Three-Point Timeout Protection
The implementation checks `!is_blocking_client_` in three critical locations:
- **Request initiation**: When making the request (if connected and first in pipeline)
- **Connection establishment**: When the connection event is Connected
- **Response handling**: When re-enabling timeout after receiving a response

This ensures timeouts are prevented at all stages of the request lifecycle.

### 3. Automatic Flag Reset
The flag automatically resets to `false` when all pending requests complete. This prevents state leakage and allows connection reuse for different command types.

### 4. Transaction Support
The implementation works for both regular clients and transaction clients by checking and setting the flag in both code paths in `makeRequestToHost()`.

### 5. Mirror Request Safety
Mirror requests always use `is_blocking_command=false` to avoid tying up mirror connections with blocking operations.

### 6. Validation and Error Handling
BLPOP timeout parameter is validated with clear error messages:
- Invalid syntax: `ERR wrong number of arguments for 'blpop' command`
- Invalid timeout: `ERR timeout is not a float or out of range`

## Testing

### Test Cases

```bash
# Configure Envoy with op_timeout: 5s
# Connect to Redis through Envoy proxy
redis-cli -p 1999

# Test 1: BLPOP with timeout > op_timeout (should work now!)
127.0.0.1:1999> BLPOP mylist 6
# Should wait up to 6 seconds, not fail at 5 seconds

# Test 2: BLPOP with data available
127.0.0.1:1999> LPUSH mylist value1 value2 value3
(integer) 3
127.0.0.1:1999> BLPOP mylist 6
1) "mylist"
2) "value3"

# Test 3: BLPOP with fractional timeout
127.0.0.1:1999> BLPOP mylist 0.5
# Should wait 0.5 seconds

# Test 4: Invalid timeout
127.0.0.1:1999> BLPOP mylist abc
(error) ERR timeout is not a float or out of range

# Test 5: Wrong number of arguments
127.0.0.1:1999> BLPOP mylist
(error) ERR wrong number of arguments for 'blpop' command

# Test 6: Negative timeout
127.0.0.1:1999> BLPOP mylist -1
(error) ERR timeout is not a float or out of range
```

## Files Modified

1. **`source/extensions/filters/network/common/redis/supported_commands.h`**
   - Added `blockingCommands()` and `isBlockingCommand()`

2. **`source/extensions/filters/network/common/redis/supported_commands.cc`**
   - Updated `isSupportedCommand()` to check blocking commands

3. **`source/extensions/filters/network/common/redis/client.h`**
   - Added `setBlockingClient()` to Client interface

4. **`source/extensions/filters/network/common/redis/client_impl.h`**
   - Added `is_blocking_client_` member variable
   - Implemented `setBlockingClient()` method

5. **`source/extensions/filters/network/common/redis/client_impl.cc`**
   - Added three timeout checks with `!is_blocking_client_`
   - Added auto-reset logic in `onRespValue()`

6. **`source/extensions/filters/network/redis_proxy/conn_pool.h`**
   - Added `is_blocking_command` parameter to interface methods

7. **`source/extensions/filters/network/redis_proxy/conn_pool_impl.h`**
   - Added `is_blocking_command` parameter to implementation methods

8. **`source/extensions/filters/network/redis_proxy/conn_pool_impl.cc`**
   - Implemented `setBlockingClient()` calls in `makeRequestToHost()`
   - Propagated `is_blocking_command` through all pool methods

9. **`source/extensions/filters/network/redis_proxy/command_splitter_impl.h`**
   - Added `BlockingRequest` class declaration

10. **`source/extensions/filters/network/redis_proxy/command_splitter_impl.cc`**
    - Implemented `BlockingRequest::create()` with validation
    - Updated `makeSingleServerRequest()` to accept and pass `is_blocking_command`

## Advantages of This Approach

1. **No Configuration Required**: Users don't need to increase `op_timeout` to accommodate BLPOP
2. **Connection Reuse**: Same connection can handle both blocking and non-blocking commands
3. **Automatic Cleanup**: Flag resets automatically, preventing state leakage
4. **Transaction Compatible**: Works with Redis transactions
5. **Mirror Safe**: Mirror requests don't block
6. **Clear Errors**: Validation provides helpful error messages
7. **Extensible**: Pattern can be reused for other blocking commands (BRPOP, BRPOPLPUSH, etc.)

## Limitations

1. **Single Command**: Currently only BLPOP is implemented
2. **Server Dependency**: Assumes Redis server respects the timeout parameter
3. **No Maximum Limit**: No enforced maximum timeout (could tie up connections indefinitely)

## Future Enhancements

### Adding Other Blocking Commands

The architecture is designed to make adding new blocking commands very simple. Here's how:

**Step 1: Add command to `blockingCommands()` set**
```cpp
// In supported_commands.h
static const absl::flat_hash_set<std::string>& blockingCommands() {
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, 
                         "blpop", 
                         "brpop",           // Add BRPOP
                         "brpoplpush",      // Add BRPOPLPUSH
                         "blmove");         // Add BLMOVE
}
```

**Step 2: Add to `writeCommands()` if it modifies state**
```cpp
// In supported_commands.h
static const absl::flat_hash_set<std::string>& writeCommands() {
  CONSTRUCT_ON_FIRST_USE(
      absl::flat_hash_set<std::string>, 
      "append", "bitfield", "blpop", "brpop", "brpoplpush", "blmove", // Add here
      // ... rest of commands
  );
}
```

**That's it!** The command will automatically:
- Be recognized as supported via `isSupportedCommand()`
- Route through `BlockingRequest::create()` for validation
- Have `is_blocking_command=true` passed through the chain
- Bypass operation timeouts via the `is_blocking_client` flag

**No changes needed in:**
- `command_splitter_impl.cc` (uses `isBlockingCommand()` check)
- `conn_pool*.cc` (already accepts `is_blocking_command` parameter)
- `client_impl.cc` (already checks `is_blocking_client_` flag)

### Commands That Can Be Added

The same pattern works for all Redis blocking commands with timeout as last parameter:
- **BRPOP** - Blocking right pop: `BRPOP key [key ...] timeout`
- **BRPOPLPUSH** - Blocking right pop + left push: `BRPOPLPUSH source destination timeout`
- **BLMOVE** - Blocking list move (Redis 6.2+): `BLMOVE source destination LEFT|RIGHT LEFT|RIGHT timeout`
- **BZPOPMIN/BZPOPMAX** - Blocking sorted set pop: `BZPOPMIN key [key ...] timeout`
- **BLMPOP** - Blocking multi-list pop (Redis 7.0+): `BLMPOP timeout numkeys key [key ...] LEFT|RIGHT`

### Potential Improvements
1. Add configuration for maximum allowed blocking timeout
2. Add metrics for blocking command usage and timeouts
3. Add support for multi-key BLPOP (currently single-key only)
4. Consider connection pool strategies for blocking vs non-blocking commands

## References

- Redis BLPOP documentation: https://redis.io/commands/blpop/
- Envoy Redis proxy filter: https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/redis_proxy_filter
