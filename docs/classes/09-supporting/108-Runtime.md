# Runtime::Loader and Snapshot

**File:** `envoy/runtime/runtime.h`  
**Implementation:** `source/common/runtime/runtime_impl.cc`

## Summary

`Runtime::Loader` provides feature flags and runtime overrides. Values come from bootstrap `layered_runtime` and optionally RTDS (Runtime Discovery Service). `Snapshot` is the per-thread immutable view. Used for gradual rollouts, kill switches, and A/B testing.

## Key Classes (from source)

### Runtime::Loader (`envoy/runtime/runtime.h`)

```cpp
class Loader {
  virtual Snapshot& snapshot() PURE;
};
```

### Snapshot (`envoy/runtime/runtime.h`)

```cpp
class Snapshot {
  virtual bool featureEnabled(absl::string_view key, uint64_t default_value) PURE;
  virtual uint64_t getInteger(absl::string_view key, uint64_t default_value) PURE;
  virtual double getDouble(absl::string_view key, double default_value) PURE;
  virtual std::string get(absl::string_view key) PURE;
};
```

### featureEnabled

- Uses `default_value` as numerator; denominator 100.
- `featureEnabled("foo", 50)` → 50% chance true (random per request).

## Source References

- `source/common/runtime/runtime_impl.cc` — LayeredRuntime
- `source/common/runtime/snapshot_impl.cc`
