# Thread-Local and Callbacks

## Pattern

Envoy is multi-threaded: main thread + worker threads. Per-worker state uses `ThreadLocal::Slot`. Cross-thread work uses `dispatcher->post()`.

## ThreadLocal::Slot (from envoy/thread_local/thread_local.h)

```cpp
class Slot {
  virtual ThreadLocalObjectSharedPtr get() PURE;
  virtual void set(InitializeCb cb) PURE;  // cb runs on each worker
  virtual void runOnAllThreads(UpdateCb update_cb) PURE;
  virtual void runOnAllThreads(UpdateCb update_cb, const std::function<void()>& complete_cb) PURE;
};
```

- `set(cb)` — Callback runs on each worker; returns object to store in slot.
- `runOnAllThreads(cb)` — Run `cb` on all workers.
- `runOnAllThreads(cb, complete_cb)` — Same, then run `complete_cb` on main when done.

## Event::Dispatcher::post

```cpp
dispatcher->post([this]() { doWork(); });
```

Schedules callback on the dispatcher's thread (event loop iteration).

## Callback Registration

Common pattern: register for events, get callbacks later.

```cpp
connection->addConnectionCallbacks(*this);  // this implements ConnectionCallbacks
// Later: onEvent(RemoteClose), onAboveWriteBufferHighWatermark(), etc.
```

## Reading Code

- `Slot::set()` — Initialize per-worker state.
- `runOnAllThreads()` — Broadcast update to workers.
- `dispatcher->post()` — Defer work to event loop.
- `addXxxCallbacks(this)` — Register for events.
