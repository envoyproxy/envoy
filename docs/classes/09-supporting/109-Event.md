# Event::Dispatcher, Timer, FileEvent

**File:** `envoy/event/dispatcher.h`  
**Implementation:** `source/common/event/`

## Summary

`Event::Dispatcher` is the event loop (libevent wrapper). It provides `createTimer`, `createFileEvent`, `post`, `run`, and `exit`. Timers and file events are the primary mechanisms for async I/O and delayed callbacks. Each worker thread has its own dispatcher.

## Key Classes (from source)

### Dispatcher (`envoy/event/dispatcher.h`)

```cpp
class Dispatcher {
  virtual Event::TimerPtr createTimer(TimerCb cb) PURE;
  virtual Event::FileEventPtr createFileEvent(
      os_fd_t fd, FileReadyCb cb, FileTriggerType trigger, uint32_t events) PURE;
  virtual void post(std::function<void()> callback) PURE;
  virtual void run(Event::RunType type) PURE;
  virtual void exit() PURE;
};
```

### Timer (`envoy/event/timer.h`)

```cpp
class Timer {
  virtual void disableTimer() PURE;
  virtual void enableTimer(const std::chrono::milliseconds& d) PURE;
  virtual bool enabled() PURE;
};
```

### FileEvent (`envoy/event/file_event.h`)

- Registers for read/write events on fd.
- `activate(events)` — Enable events.
- `resetFileEvents()` — Disable.

## Source References

- `source/common/event/dispatcher_impl.cc` — LibeventScheduler
- Used by: ConnectionImpl, ListenerFilterBufferImpl, all async code
