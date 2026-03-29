# Session Idle List — `session_idle_list.h`

**Files:**
- `source/common/http/session_idle_list.h`
- `source/common/http/session_idle_list_interface.h`
- `source/common/http/session_idle_list.cc`

`SessionIdleList` manages a set of idle HTTP sessions that can be terminated when the
system is overloaded. It is used by the Overload Manager to shed load by closing
long-lived idle connections in a controlled, rate-limited manner.

---

## Class Overview

```mermaid
classDiagram
    class SessionIdleListInterface {
        <<interface>>
        +AddSession(session)*
        +RemoveSession(session)*
        +MaybeTerminateIdleSessions(is_saturated)*
    }

    class SessionIdleList {
        +AddSession(session)
        +RemoveSession(session)
        +MaybeTerminateIdleSessions(is_saturated)
        +set_min_time_before_termination_allowed(duration)
        +set_max_sessions_to_terminate_in_one_round(n)
        +set_max_sessions_to_terminate_in_one_round_when_saturated(n)
        +set_ignore_min_time_before_termination_allowed(bool)
        -idle_sessions_ IdleSessions
        -min_time_before_termination_allowed_ = 1 min
        -max_sessions_to_terminate_in_one_round_ = 5
        -max_sessions_to_terminate_in_one_round_when_saturated_ = 50
        -dispatcher_
    }

    class IdleSessions {
        +AddSessionToList(enqueue_time, session)
        +RemoveSessionFromList(session)
        +GetEnqueueTime(session) MonotonicTime
        +next_session_to_terminate() IdleSessionInterface&
        +size() size_t
        -set_ IdleSessionSet  sorted by enqueue_time
        -map_ IdleSessionMap  O(1) lookup by pointer
    }

    class IdleSessionInterface {
        <<interface>>
        +terminate()*
    }

    SessionIdleListInterface <|-- SessionIdleList
    SessionIdleList o-- IdleSessions
    IdleSessions o-- "0..*" IdleSessionInterface
```

---

## Dual Data Structure Design

`IdleSessions` maintains two parallel structures for O(1) operations in both directions:

| Structure | Type | Purpose |
|---|---|---|
| `set_` | `absl::btree_set<SessionInfo>` sorted by `(enqueue_time, session*)` | Ordered iteration — find oldest session first |
| `map_` | `absl::node_hash_map<IdleSessionInterface*, SessionInfo>` | O(1) lookup by pointer — for `RemoveSession` and `GetEnqueueTime` |

```mermaid
flowchart LR
    subgraph btree_set sorted by enqueue_time
        S1[session_A t=10]
        S2[session_B t=20]
        S3[session_C t=35]
    end
    subgraph node_hash_map keyed by pointer
        M1[ptr_A → SessionInfo_A]
        M2[ptr_B → SessionInfo_B]
        M3[ptr_C → SessionInfo_C]
    end

    S1 <-.->|same SessionInfo| M1
    S2 <-.->|same SessionInfo| M2
    S3 <-.->|same SessionInfo| M3
```

- `AddSession` → inserts into both set and map
- `RemoveSession` → looks up in map (O(1)), then removes from both
- `MaybeTerminateIdleSessions` → iterates `set_.begin()` (oldest first)

---

## Termination Logic — `MaybeTerminateIdleSessions(is_saturated)`

Called by the worker thread when the Overload Manager fires a termination action.

```mermaid
flowchart TD
    A[MaybeTerminateIdleSessions is_saturated] --> B[limit = MaxSessionsToTerminateInOneRound]
    B --> C[min_age = MinTimeBeforeTerminationAllowed]
    C --> D{idle_sessions_.size > 0?}
    D -->|No| Z[return - nothing to terminate]
    D -->|Yes| E[session = idle_sessions_.next_session_to_terminate - oldest]
    E --> F{ignore_min_time OR\nenqueue_time + min_age <= now?}
    F -->|No - session too young| Z
    F -->|Yes - eligible| G[session.terminate]
    G --> H{terminated_count < limit?}
    H -->|Yes| E
    H -->|No - rate limit hit| Z
```

### Rate Limits

| Condition | Max terminations per round |
|---|---|
| Normal (`is_saturated = false`) | `max_sessions_to_terminate_in_one_round_` = **5** |
| Saturated (`is_saturated = true`) | `max_sessions_to_terminate_in_one_round_when_saturated_` = **50** |

Defaults are defined as compile-time constants:
```cpp
constexpr size_t kMaxSessionsToTerminateInOneRound = 5;
constexpr size_t kMaxSessionsToTerminateInOneRoundWhenSaturated = 50;
```

### Age Guard

`min_time_before_termination_allowed_` (default **1 minute**) prevents recently-established
sessions from being immediately terminated during overload. Only sessions that have been
in the idle list for at least this duration are eligible.

Setting `ignore_min_time_before_termination_allowed_ = true` bypasses this check — used
in tests and extreme overload scenarios.

---

## Usage Pattern

```mermaid
sequenceDiagram
    participant HCM as ConnectionManagerImpl
    participant SIL as SessionIdleList
    participant OM as OverloadManager

    Note over HCM,SIL: Connection becomes idle (no active streams)
    HCM->>SIL: AddSession(this)

    Note over HCM,SIL: New request arrives on connection
    HCM->>SIL: RemoveSession(this)

    Note over OM,SIL: System overload action fires
    OM->>SIL: MaybeTerminateIdleSessions(is_saturated)
    SIL->>SIL: find oldest eligible sessions
    SIL->>HCM: session.terminate()
    Note over HCM: Connection closed
```

---

## Configuration

All limits are set via setters (typically by the Overload Manager integration layer):

| Setter | Default | Description |
|---|---|---|
| `set_min_time_before_termination_allowed(d)` | 1 minute | Minimum age before a session can be terminated |
| `set_max_sessions_to_terminate_in_one_round(n)` | 5 | Max terminations per invocation under normal load |
| `set_max_sessions_to_terminate_in_one_round_when_saturated(n)` | 50 | Max terminations per invocation under saturation |
| `set_ignore_min_time_before_termination_allowed(bool)` | false | Skip age check |
