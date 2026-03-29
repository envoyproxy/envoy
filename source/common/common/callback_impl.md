# Callback Manager — `callback_impl.h`

**File:** `source/common/common/callback_impl.h`

Two complementary callback managers used throughout Envoy for observer/event patterns:
`CallbackManager<ReturnType, Args...>` for single-thread use, and
`ThreadSafeCallbackManager` for cross-thread notification.

---

## Class Overview

```mermaid
classDiagram
    class CallbackHandle {
        <<interface envoy/common/callback.h>>
        +~CallbackHandle()   removes itself on destruction
    }

    class CallbackManager~ReturnType, ...Args~ {
        +add(callback) CallbackHandlePtr
        +runCallbacks(args...) ReturnType
        +runCallbacksWith(run_with) ReturnType
        +size() size_t
        -callbacks_ list~CallbackHolder ptr~
        -still_alive_ shared_ptr~bool~
    }

    class CallbackHolder {
        +~CallbackHolder()  removes from parent list
        -parent_ CallbackManager
        -cb_ Callback
        -still_alive_ weak_ptr~bool~
        -it_ list_iterator
    }

    class ThreadSafeCallbackManager {
        +create()$ shared_ptr~TSCM~
        +add(dispatcher, callback) CallbackHandlePtr
        +runCallbacks()
        +size() size_t
        -callbacks_ list~CallbackListEntry~  GUARDED_BY lock_
        -lock_ MutexBasicLockable
    }

    CallbackHandle <|-- CallbackHolder
    CallbackManager o-- CallbackHolder
    ThreadSafeCallbackManager o-- CallbackHolder
```

---

## `CallbackManager<ReturnType, Args...>`

Single-threaded callback list. All methods must be called from the same thread.

### Lifecycle

```mermaid
sequenceDiagram
    participant Owner as Object owning callbacks
    participant CM as CallbackManager
    participant Subscriber as Subscriber

    Subscriber->>CM: add(callback) → handle
    CM->>CM: store CallbackHolder* in callbacks_ list
    CM->>Subscriber: returns CallbackHandlePtr (owns CallbackHolder)

    Owner->>CM: runCallbacks(args...)
    CM->>CM: iterate callbacks_, invoke each cb_(args...)

    Subscriber->>Subscriber: handle goes out of scope (RAII)
    Note over Subscriber: CallbackHolder destructor
    CM->>CM: callbacks_.erase(it_)  O(1) via stored iterator
```

### Thread Safety

**Not thread-safe.** All `add`, `runCallbacks`, and handle destruction must happen
on the same thread. `CallbackHolder::~CallbackHolder` uses `still_alive_.expired()`
(weak_ptr check) to detect if the manager has already been destroyed, preventing
use-after-free when callbacks outlive their manager.

### Safe Self-Removal During Iteration

```
runCallbacks iterates with: auto current = *(it++);
```
Pre-incrementing the iterator before invoking the callback means a callback can
destroy its own handle (`RAII`) and splice itself out of `callbacks_` safely.
However, destroying *another* callback's handle during iteration is **not safe** —
that would invalidate the iterator `it` already in use.

### `ReturnType` Specialization

When `ReturnType = absl::Status`, `runCallbacks` stops iteration and returns
the first non-OK status (`RETURN_IF_NOT_OK`). For `void`, all callbacks are invoked
unconditionally.

### `runCallbacksWith`

Generates fresh arguments per callback via a factory lambda, useful when arguments
are expensive to construct or must be re-built for each subscriber:

```cpp
manager.runCallbacksWith([]() -> std::tuple<Foo, Bar> {
    return {compute_foo(), compute_bar()};
});
```

---

## `ThreadSafeCallbackManager`

Cross-thread callback manager. Callbacks are registered with a `Dispatcher`, so when
`runCallbacks()` is called from any thread, each callback is **posted** to its
registration dispatcher rather than invoked inline.

### Design

```mermaid
sequenceDiagram
    participant T1 as Thread 1 (caller)
    participant TSCM as ThreadSafeCallbackManager
    participant D as Subscriber Dispatcher (Thread 2)
    participant CB as Callback function

    T1->>TSCM: add(dispatcher_on_thread2, cb)
    Note over TSCM: stores (CallbackHolder*, dispatcher, still_alive)

    T1->>TSCM: runCallbacks()
    TSCM->>TSCM: lock, iterate callbacks_
    TSCM->>D: dispatcher.post([still_alive, cb]{ if !expired → cb() })
    Note over T1: returns immediately, callbacks run async

    D->>CB: cb() (on Thread 2's event loop)
```

**Must be held as `shared_ptr`** — `create()` factory enforces this. The
`shared_ptr<ThreadSafeCallbackManager>` is stored in each `CallbackHolder` to keep
the manager alive while there are pending posted callbacks.

### Cancellation Safety

Each `CallbackHolder` holds its own `shared_ptr<bool> still_alive`. When the holder
is destroyed (handle dropped), `still_alive` becomes orphaned (shared_ptr refcount
drops to 0 → bool destroyed). The posted lambda checks `if (!still_alive.expired())`
before invoking the callback — protecting against calling back into a destroyed object.

---

## Usage Patterns in Envoy

| Use case | Manager type | Example |
|---|---|---|
| Cluster update listeners (same thread) | `CallbackManager<void, ClusterInfo>` | `ClusterManagerImpl` notifying health checkers |
| Runtime feature flag watchers | `CallbackManager<void>` | `Runtime::Loader::addUpdateCallback` |
| Worker-to-main thread notifications | `ThreadSafeCallbackManager` | Overload manager callbacks across workers |
| LDS/RDS update callbacks | `CallbackManager<absl::Status>` | Stops propagation on first error |

### RAII Handle Pattern

The `ABSL_MUST_USE_RESULT` annotation on `add()` enforces storing the handle:

```cpp
// WRONG — handle immediately destroyed, callback never fires
manager.add(my_callback);  // compiler warning

// CORRECT
auto handle_ = manager.add(my_callback);  // stored as member
```
