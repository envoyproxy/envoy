# Supporting Subsystems

This folder documents supporting classes used across Envoy: **StreamInfo**, **Runtime**, **Event**, **Init**, **Overload**, **ThreadLocal**. All classes are verified against the source code.

## Source Paths

| Subsystem | Path |
|-----------|------|
| StreamInfo | `envoy/stream_info/stream_info.h`, `source/common/stream_info/` |
| Runtime | `envoy/runtime/runtime.h`, `source/common/runtime/` |
| Event | `envoy/event/dispatcher.h`, `source/common/event/` |
| Init | `envoy/init/manager.h`, `source/common/init/` |
| Overload | `envoy/server/overload/`, `source/server/overload/` |
| ThreadLocal | `envoy/thread_local/thread_local.h`, `source/common/thread_local/` |

## Key Documents

- [107-StreamInfo](107-StreamInfo.md) — StreamInfo, StreamInfoImpl, ResponseFlag
- [108-Runtime](108-Runtime.md) — Runtime::Loader, LayeredRuntime, Snapshot
- [109-Event](109-Event.md) — Event::Dispatcher, Timer, FileEvent
- [110-Init](110-Init.md) — Init::Manager, Init::Target
- [111-ThreadLocal](111-ThreadLocal.md) — ThreadLocal::Instance, Slot, TypedSlot
