# Init::Manager and Init::Target

**File:** `envoy/init/manager.h`  
**Implementation:** `source/common/init/manager_impl.cc`

## Summary

`Init::Manager` coordinates ordered initialization. `Init::Target` represents a component that must initialize; it calls `initialize()` when ready. The manager waits for all targets before marking ready. Used for cluster initialization, xDS readiness, and dependency ordering.

## Key Classes (from source)

### Init::Manager (`envoy/init/manager.h`)

```cpp
class Manager {
  virtual void add(const Target& target) PURE;
  virtual void initialize(InitializeFn fn) PURE;
  virtual State state() const PURE;  // NotReady, Ready
};
```

### Init::Target (`envoy/init/target.h`)

```cpp
class Target {
  virtual std::string name() const PURE;
  void initialize(InitializeFn fn);  // Calls fn when ready
};
```

### Flow

1. Components register as `Init::Target` via `add()`.
2. Each target calls `initialize(callback)` when ready.
3. Manager invokes user `InitializeFn` when all targets ready.

## Source References

- `source/common/init/manager_impl.cc`
- Used by: ClusterManager init, xDS subscriptions
