# ThreadLocal::Instance and Slot

**File:** `envoy/thread_local/thread_local.h`  
**Implementation:** `source/common/thread_local/thread_local_impl.cc`

## Summary

Envoy uses thread-local storage for per-worker state. `ThreadLocal::Instance` allocates `Slot`s. Each slot holds a `ThreadLocalObject` per thread. `set(InitializeCb)` runs the callback on each worker to create/update the object. Used for cluster manager, connection pools, and other per-worker state.

## Key Classes (from source)

### Slot (`envoy/thread_local/thread_local.h`)

```cpp
class Slot {
  virtual bool currentThreadRegistered() PURE;
  virtual ThreadLocalObjectSharedPtr get() PURE;
  virtual void set(InitializeCb cb) PURE;
  virtual void runOnAllThreads(UpdateCb update_cb) PURE;
};
```

### TypedSlot<T>

- `getTyped<T>()` — Cast to T.
- `runOnAllThreads(cb, complete_cb)` — Run on all workers; complete_cb on main when done.

### Flow

1. Main thread allocates `Slot` via `allocateSlot()`.
2. `set(InitializeCb)` — Callback runs on each worker, returns object to store.
3. Workers call `get()` or `getTyped<T>()` to access their copy.

## Source References

- `source/common/thread_local/thread_local_impl.cc`
- Used by: ClusterManager, Http::ConnectionPool, Router
