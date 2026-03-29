# Interfaces and PURE

## Pattern

Envoy uses abstract interfaces with pure virtual methods. The `PURE` macro expands to `= 0` (from `envoy/common/pure.h`).

```cpp
// envoy/common/pure.h
#define PURE = 0

// envoy/network/connection.h
class Connection : public Event::DeferredDeletable, public FilterManager {
  virtual void close(ConnectionCloseType type) PURE;
  virtual State state() PURE;
};

// Callback interfaces use PURE for virtual methods
class ConnectionCallbacks {
  virtual void onEvent(ConnectionEvent event) PURE;
  virtual void onAboveWriteBufferHighWatermark() PURE;
  virtual void onBelowWriteBufferLowWatermark() PURE;
};
```

## Why

- **Testability:** Mocks implement interfaces.
- **Extensibility:** New implementations without changing callers.
- **Header-only:** Interface in `envoy/`; implementation in `source/`.

## Naming

- Interfaces often live in `envoy/<domain>/` (e.g. `envoy/network/connection.h`).
- Implementations in `source/common/<domain>/` or `source/extensions/`.
- Interface names may omit `Impl` (e.g. `Connection` vs `ConnectionImpl`).

## Reading Code

When you see `PURE` in a header, it's an interface. Look for classes that inherit and override those methods.
