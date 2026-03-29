# Smart Pointers and Ownership

## Pattern

Envoy consistently uses type aliases for smart pointers:

| Alias | Meaning | Usage |
|-------|---------|-------|
| `XxxPtr` | `std::unique_ptr<Xxx>` | Exclusive ownership |
| `XxxSharedPtr` | `std::shared_ptr<Xxx>` | Shared ownership |
| `XxxConstSharedPtr` | `std::shared_ptr<const Xxx>` | Shared, read-only |
| `XxxOptRef` | `absl::optional<std::reference_wrapper<Xxx>>` | Optional reference |

## Examples (from source)

```cpp
// envoy/network/connection.h
using ConnectionPtr = std::unique_ptr<Connection>;

// envoy/upstream/upstream.h
using HostConstSharedPtr = std::shared_ptr<const Host>;

// envoy/common/optref.h
template <class T> struct OptRef;
```

## Ownership Conventions

- **UniquePtr:** Single owner; transferred via `std::move()`.
- **SharedPtr:** Multiple owners; used for config, callbacks, long-lived objects.
- **Raw pointer / reference:** Non-owning; lifetime guaranteed by caller.

## Reading Code

- `foo(Ptr x)` — Caller transfers ownership.
- `foo(SharedPtr x)` — Shared; may outlive call.
- `foo(Xxx& x)` — Non-owning reference; must not be null.
