# HappyEyeballsConnectionImpl

**Files:**
- `source/common/network/multi_connection_base_impl.h/.cc` (base)
- `source/common/network/happy_eyeballs_connection_impl.h/.cc`
**Namespace:** `Envoy::Network`

## Overview

`HappyEyeballsConnectionImpl` implements **RFC 8305 (Happy Eyeballs v2)** for upstream TCP connections. When an upstream hostname resolves to multiple addresses (e.g., both IPv6 and IPv4), it races connections to each address in priority order, using the first one that successfully connects while cancelling the others.

`MultiConnectionBaseImpl` is the generic racing base; `HappyEyeballsConnectionImpl` extends it with RFC 8305–compliant address ordering and the 250ms per-attempt delay.

## Class Hierarchy

```mermaid
classDiagram
    class MultiConnectionBaseImpl {
        +connect()
        +write(buffer, end_stream)
        +addReadFilter(filter)
        +setBufferLimits(bytes)
        +close(type)
        -connections_: vector~ClientConnectionPtr~
        -post_connect_state_: PostConnectState
        -next_attempt_timer_: TimerPtr
    }

    class HappyEyeballsConnectionImpl {
        +connect()
        -address_list_: vector~Address::InstanceConstSharedPtr~
        -current_attempt_: size_t
        -connection_attempt_delay_: Duration
    }

    class ConnectionProvider {
        <<interface>>
        +createNextConnection(): ClientConnectionPtr
        +hasNextConnection(): bool
        +totalConnections(): size_t
    }

    class HappyEyeballsConnectionProvider {
        -address_list_: vector~AddressPtr~
        -factory_: ClientConnectionFactory
        -index_: size_t
    }

    class ClientConnectionImpl

    MultiConnectionBaseImpl <|-- HappyEyeballsConnectionImpl
    ConnectionProvider <|-- HappyEyeballsConnectionProvider
    HappyEyeballsConnectionImpl --> HappyEyeballsConnectionProvider
    MultiConnectionBaseImpl *-- ClientConnectionImpl : connections_
```

## RFC 8305 Address Ordering

Before attempting connections, addresses are sorted per RFC 8305:

```mermaid
flowchart TD
    Raw["DNS resolved addresses:<br/>[2001:db8::1, 192.0.2.1, 2001:db8::2, 192.0.2.2]"] --> Sort["RFC 8305 sort:<br/>1. Interleave IPv6 and IPv4<br/>2. Same-family sorted by preference<br/>3. Happy eyeballs interleaving"]
    Sort --> Ordered["Ordered list:<br/>[2001:db8::1, 192.0.2.1, 2001:db8::2, 192.0.2.2]"]
    Ordered --> Attempt["Attempt connections in order<br/>with 250ms delay between each"]
```

## Connection Racing Flow

```mermaid
sequenceDiagram
    participant Caller as ClusterManager
    participant HE as HappyEyeballsConnectionImpl
    participant C1 as ClientConn (IPv6)
    participant C2 as ClientConn (IPv4)
    participant Timer as next_attempt_timer_

    Caller->>HE: connect()
    HE->>C1: connect() to IPv6 address
    HE->>Timer: start(250ms)

    alt IPv6 connects fast
        C1-->>HE: onEvent(Connected)
        HE->>C2: close() (cancel)
        HE->>Timer: disable
        HE->>Caller: onEvent(Connected)
    else Timer fires first
        Timer->>HE: callback
        HE->>C2: connect() to IPv4 address
        alt IPv6 wins eventually
            C1-->>HE: onEvent(Connected)
            HE->>C2: close()
            HE->>Caller: onEvent(Connected)
        else IPv4 wins
            C2-->>HE: onEvent(Connected)
            HE->>C1: close()
            HE->>Caller: onEvent(Connected)
        else Both fail
            C1-->>HE: onEvent(RemoteClose)
            C2-->>HE: onEvent(RemoteClose)
            HE->>Caller: onEvent(RemoteClose)
        end
    end
```

## `PostConnectState` — Deferred Operations

Before a winner is selected, operations like `write()`, `addReadFilter()`, and `setBufferLimits()` are deferred and replayed on the winning connection:

```mermaid
flowchart TD
    subgraph "Before winner selected"
        Op1["addReadFilter(http_codec_filter)"] --> PS["PostConnectState queue"]
        Op2["write(request_headers)"] --> PS
        Op3["setBufferLimits(65536)"] --> PS
    end

    Winner["Winner connection selected (e.g. IPv6)"] --> Replay["Replay PostConnectState on winner:<br/>1. addReadFilter<br/>2. setBufferLimits<br/>3. write"]
    PS --> Replay
```

## `PerConnectionState` — Applied Immediately to All

Some state must be applied to every candidate connection (not deferred):

| State | Reason Applied to All |
|-------|----------------------|
| `setBufferLimits()` | Watermarks must be consistent across all attempts |
| `noDelay(true)` | TCP_NODELAY applied immediately on socket creation |
| `addConnectionCallbacks()` | Internal callbacks needed for race tracking |

## Winner Selection State Machine

```mermaid
stateDiagram-v2
    [*] --> Racing : connect() called
    Racing --> Racing : more attempts started (timer)
    Racing --> WinnerSelected : first onEvent(Connected) received
    WinnerSelected --> Active : PostConnectState replayed on winner
    Active --> Active : normal I/O
    Active --> Closed : close() or remote close
    Racing --> AllFailed : all candidate connections failed
    AllFailed --> [*] : propagate failure to caller
    Closed --> [*]
```

## Cancellation and Cleanup

When a winner is selected, all losing connections are closed:

```mermaid
sequenceDiagram
    participant HE as HappyEyeballsConnectionImpl
    participant Winner as Winning Connection
    participant Loser1 as Losing Connection 1
    participant Loser2 as Losing Connection 2

    HE->>Winner: onEvent(Connected) received
    HE->>HE: winnerSelected = Winner
    HE->>Loser1: removeConnectionCallbacks()
    HE->>Loser1: close(NoFlush)
    HE->>Loser2: removeConnectionCallbacks()
    HE->>Loser2: close(NoFlush)
    HE->>HE: Replay PostConnectState on Winner
    HE->>Caller: onEvent(Connected)
```

## Attempt Timing

| Parameter | Default | Purpose |
|-----------|---------|---------|
| `connection_attempt_delay` | 250ms (RFC 8305) | Delay before starting next address attempt |
| Max attempts | `address_list_.size()` | One attempt per resolved address |

## Key Design Properties

- **Transparent substitution**: `HappyEyeballsConnectionImpl` implements the same `ClientConnection` interface as `ClientConnectionImpl`, so upstream pool code needs no changes.
- **No extra latency when first address succeeds**: If the first connection (typically IPv6) succeeds before the 250ms timer fires, no additional connection is ever made.
- **Filter/callback replay**: All `addReadFilter()`, `addWriteFilter()`, `addConnectionCallbacks()`, and initial `write()` calls are safely deferred and replayed exactly once on the winner.
