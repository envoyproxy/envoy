# OptRef and Optional

## Pattern

Envoy uses `OptRef<T>` for optional references (from `envoy/common/optref.h`). Lighter than `absl::optional<std::reference_wrapper<T>>` — 8 bytes vs 16+.

## OptRef (from envoy/common/optref.h)

```cpp
template <class T> struct OptRef {
  OptRef(T& t);
  OptRef();
  OptRef(absl::nullopt_t);

  T* operator->() const;
  T* ptr() const;
  T& ref() const;
  T& operator*() const;
  bool has_value() const;
  T& value_or(T& other) const;
  operator bool() const;

  void emplace(T& ref);
  void reset();
  absl::optional<T> copy() const;
};

// Helpers
makeOptRef(T& ref) -> OptRef<T>;
makeOptRefFromPtr(T* ptr) -> OptRef<T>;  // nullopt if ptr==nullptr
```

## Usage

```cpp
void foo(OptRef<Bar> bar) {
  if (bar) {
    bar->method();
  }
}
```

## absl::optional

Used for optional values (not references):

```cpp
absl::optional<uint32_t> port;
if (port.has_value()) {
  use(*port);
}
```

## Reading Code

- `OptRef<T>` — Optional reference; 8 bytes (pointer). Check `has_value()` or `if (opt_ref)` before use.
- `absl::optional<T>` — Optional value; used for config fields, etc.
