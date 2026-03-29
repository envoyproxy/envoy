# NonCopyable and Mixins

## Pattern

Envoy uses `NonCopyable` (from `source/common/common/non_copyable.h`) to disable copy and move for classes that manage resources or have unique identity.

## NonCopyable (from source)

```cpp
// source/common/common/non_copyable.h
class NonCopyable {
 protected:
  NonCopyable() = default;
  NonCopyable(NonCopyable&&) noexcept = delete;
  NonCopyable& operator=(NonCopyable&&) noexcept = delete;
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
};
```

## Usage (from source)

```cpp
// source/common/thread_local/thread_local_impl.h
class InstanceImpl : public NonCopyable, public Instance { ... };

// source/common/singleton/manager_impl.h
class ManagerImpl : public Manager, NonCopyable { ... };
```

## Why

- **Ownership clarity:** Non-copyable objects are typically owned by `unique_ptr` or a single parent.
- **Resource safety:** Prevents accidental copies of connections, listeners, etc.
- **Explicit transfer:** Use `std::move()` with `unique_ptr` instead of implicit copies.

## Reading Code

- `class X : public NonCopyable` — X cannot be copied or moved. Pass by pointer or reference.
- Often paired with `unique_ptr<X>` for ownership.
