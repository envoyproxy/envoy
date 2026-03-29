# Overload Manager — `overload_manager_impl.h`

**File:** `source/server/overload_manager_impl.h`

`OverloadManagerImpl` monitors system resources (memory, file descriptors, CPU, heap
size) on a timer and translates raw utilization values into `OverloadActionState` values
that are dispatched to all worker threads. Workers react by throttling, shedding, or
terminating connections and streams.

---

## Class Overview

```mermaid
classDiagram
    class OverloadManager {
        <<interface>>
        +start()*
        +registerForAction(action, dispatcher, cb)*
        +getThreadLocalOverloadState()*
        +getLoadShedPoint(name)*
        +scaledTimerFactory()*
        +stop()*
    }

    class OverloadManagerImpl {
        +start()
        +registerForAction(action, dispatcher, cb)
        +getThreadLocalOverloadState()
        +getLoadShedPoint(name)
        +scaledTimerFactory()
        +stop()
        -updateResourcePressure(resource, pressure, epoch)
        -flushResourceUpdates()
        -resources_ node_hash_map~string,Resource~
        -actions_ node_hash_map~Symbol,OverloadAction~
        -loadshed_points_ flat_hash_map~string,LoadShedPointImpl~
        -tls_ TypedSlot~ThreadLocalOverloadStateImpl~
        -action_symbol_table_
        -timer_ refresh timer
        -state_updates_to_flush_
        -callbacks_to_flush_
    }

    class Resource {
        +onSuccess(usage)
        +onFailure(error)
        +update(flush_epoch)
        -name_
        -monitor_ ResourceMonitorPtr
        -pending_update_ bool
        -flush_epoch_
        -pressure_gauge_
    }

    class OverloadAction {
        +updateResourcePressure(name, pressure) bool
        +getState() OverloadActionState
        -triggers_ node_hash_map~string,TriggerPtr~
        -state_ OverloadActionState
    }

    class Trigger {
        <<interface>>
        +updateValue(double)*
        +actionState()*
    }

    class ThresholdTrigger {
        -threshold_ double
    }

    class ScaledTrigger {
        -scaling_threshold_ double
        -saturation_threshold_ double
    }

    class LoadShedPointImpl {
        +shouldShedLoad() bool
        +updateResource(name, utilization)
        -triggers_ flat_hash_map~string,TriggerPtr~
        -probability_shed_load_ atomic~float~
    }

    class NamedOverloadActionSymbolTable {
        +get(name) Symbol
        +lookup(name) optional~Symbol~
        +name(symbol) string_view
        +size()
    }

    OverloadManager <|-- OverloadManagerImpl
    OverloadManagerImpl o-- "1..*" Resource
    OverloadManagerImpl o-- "1..*" OverloadAction
    OverloadManagerImpl o-- "0..*" LoadShedPointImpl
    OverloadManagerImpl o-- NamedOverloadActionSymbolTable
    OverloadAction o-- "1..*" Trigger
    LoadShedPointImpl o-- "1..*" Trigger
    Trigger <|-- ThresholdTrigger
    Trigger <|-- ScaledTrigger
```

---

## Data Flow: Resource → Action → Worker Thread

```mermaid
sequenceDiagram
    participant Timer as refresh timer (main thread)
    participant Res as Resource
    participant Monitor as ResourceMonitor
    participant OM as OverloadManagerImpl
    participant Action as OverloadAction
    participant TLS as ThreadLocalOverloadState (per worker)
    participant Worker as Worker callback

    Timer->>OM: timer fires (refresh_interval_)
    loop each Resource
        OM->>Res: update(flush_epoch)
        Res->>Monitor: updateResourceUsage(callbacks)
        Monitor-->>Res: onSuccess(ResourceUsage{pressure})
        Res->>OM: updateResourcePressure(name, pressure, epoch)
    end
    Note over OM: last resource reports back
    OM->>OM: flushResourceUpdates()
    loop each changed OverloadAction
        OM->>Action: updateResourcePressure(name, pressure)
        Action->>Action: recompute state = max(all triggers)
        alt state changed
            OM->>TLS: setState(symbol, new_state) via TLS slot set()
            OM->>Worker: callback(new_state) dispatched to worker's dispatcher
        end
    end
```

### Flush Epoch

`flush_epoch_` is incremented on each refresh cycle. Each `Resource` stores
`flush_epoch_` when it calls `update()`. When `onSuccess` fires, the resource
compares its epoch with the current epoch — if the epoch has advanced (i.e., a
later cycle started before this resource's async callback returned), the update
is skipped (`skipped_updates_counter_` incremented). This prevents stale
resource readings from overwriting fresher ones.

---

## `Trigger` — Resource → Action Mapping

Two trigger types translate a raw `double` pressure value (0.0–1.0+) into an
`OverloadActionState`:

### `ThresholdTrigger`

```
if pressure >= threshold → SATURATED (1.0)
else                     → NEUTRAL   (0.0)
```

Binary flip at the configured threshold.

### `ScaledTrigger`

```
if pressure <  scaling_threshold  → NEUTRAL (0.0)
if pressure >= saturation_threshold → SATURATED (1.0)
else → linear scale between the two thresholds
     → OverloadActionState(scale_percent)
```

`OverloadActionState::value()` returns a float in `[0.0, 1.0]` where 0 = neutral
and 1 = fully saturated. Used by `ScaledRangeTimer` to linearly compress timer
ranges under load.

---

## `OverloadAction`

Each `OverloadAction` has a set of `Trigger`s, one per configured resource. Its
`state_` is the **maximum** `OverloadActionState` across all triggers. When any
trigger changes, `getState()` returns the new max and the action notifies all
registered callbacks.

```cpp
bool OverloadAction::updateResourcePressure(const std::string& name, double pressure) {
    auto& trigger = triggers_[name];
    bool changed = trigger->updateValue(pressure);
    if (changed) {
        state_ = std::max over all triggers of trigger->actionState();
    }
    return changed;
}
```

---

## `LoadShedPointImpl`

`LoadShedPoint` is a probabilistic shedding mechanism checked at specific points
in the request/connection lifecycle (e.g., before accepting a new connection, before
allocating a new stream).

```cpp
bool LoadShedPointImpl::shouldShedLoad() {
    return random_generator_.bernoulli(probability_shed_load_.load());
}
```

`probability_shed_load_` is an `atomic<float>` updated on the main thread and read
lock-free from any worker thread. The probability is the max of all trigger scale
values — e.g., a `ScaledTrigger` at 60% resource pressure yields 60% probability
of shedding.

Built-in load shed points (from `OverloadActionNames`):
- `envoy.load_shed_points.tcp_listener_accept` — shed before accepting new TCP connections
- `envoy.load_shed_points.http_connection_manager_decode_headers` — shed before decoding headers
- `envoy.load_shed_points.http1_server_abort_dispatch` — shed during H1 dispatch
- `envoy.load_shed_points.http2_server_go_away_on_dispatch` — send GOAWAY under load

---

## `NamedOverloadActionSymbolTable`

Maps action names (strings) to compact integer `Symbol` indices for O(1) dispatch
to the correct TLS slot entry without string comparisons on the hot path.

```cpp
Symbol s = table_.get("envoy.overload_actions.stop_accepting_requests");
// s.index() == 3  (stable across the process lifetime)
```

Symbols are guaranteed contiguous from 0, which allows `ThreadLocalOverloadStateImpl`
to use a `std::vector` indexed by symbol for direct O(1) state lookup.

---

## `ThreadLocalOverloadStateImpl`

Stored per worker thread via `tls_`. Main thread flushes via `tls_.set(...)` to
propagate updated `OverloadActionState` values atomically.

Workers call:
```cpp
OverloadActionState state = tls_->getState(action_symbol);
```
This is a `std::vector` index — no lock, no hash lookup.

---

## Built-in Overload Actions

Configured in the bootstrap `overload_manager` proto field. Common production actions:

| Action name | Typical trigger | Effect |
|---|---|---|
| `envoy.overload_actions.stop_accepting_requests` | heap size ≥ 95% | Worker calls `stopAcceptingConnectionsCb` |
| `envoy.overload_actions.stop_accepting_connections` | heap size ≥ 95% | Listener disables accept |
| `envoy.overload_actions.disable_http_keepalive` | heap size ≥ 85% | HCM adds `Connection: close` |
| `envoy.overload_actions.reset_high_memory_stream` | heap size ≥ 90% | Worker resets streams by memory usage |
| `envoy.overload_actions.reject_incoming_connections` | fd count ≥ 80% | TCP listener rejects accept |

---

## `scaledTimerFactory()`

Returns a factory that creates `ScaledRangeTimerManager` instances. Each worker
gets its own manager. Under the `ScaledMinimumTimer` action, timers fire sooner
than their configured maximum to shed idle connections more aggressively.

```mermaid
flowchart LR
    A[idle_timeout = 300s configured] --> B[ScaledRangeTimer min=1s max=300s]
    C[overload action scale = 0.5] --> D[timer fires at 1s + 0.5 * 299s = 150.5s]
    E[overload action scale = 1.0 saturated] --> F[timer fires at 1s minimum]
```
