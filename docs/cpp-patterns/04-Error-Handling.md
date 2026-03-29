# Error Handling: StatusOr, RETURN_IF_NOT_OK

## Pattern

Envoy uses `absl::StatusOr<T>` for functions that can fail. Macros simplify propagation.

## StatusOr (from source/common/common/statusor.h)

```cpp
using absl::StatusOr;  // Envoy::StatusOr<T>

Envoy::StatusOr<int> Foo() {
  if (error) return CodecProtocolError("Invalid protocol");
  return 123456;
}

void Bar() {
  auto status_or = Foo();
  if (status_or.ok()) {
    int result = status_or.value();
  } else {
    // status_or.status() has error
  }
}
```

## Macros (from envoy/common/exception.h)

| Macro | Purpose |
|-------|---------|
| `RETURN_IF_NOT_OK(status_fn)` | If `status_fn` returns error Status, return it. |
| `RETURN_IF_NOT_OK_REF(variable)` | If `variable` (StatusOr) is !ok(), return `variable.status()`. |
| `SET_AND_RETURN_IF_NOT_OK(check_status, set_status)` | If `check_status` is error, assign to `set_status` and return. |
| `THROW_IF_NOT_OK(status_fn)` | If Status is error, throw EnvoyException. |
| `THROW_IF_NOT_OK_REF(status)` | Same for StatusOr.status(). |
| `THROW_OR_RETURN_VALUE(expr, type)` | If `expr` (StatusOr) is error, throw; else return `expr.value()`. |

## Examples (from source/common/upstream/upstream_impl.cc)

```cpp
RETURN_IF_NOT_OK(Envoy::Config::Utility::translateOpaqueConfig(...));

auto object_or_error = factory->createProtocolOptionsConfig(...);
RETURN_IF_NOT_OK_REF(object_or_error.status());
options[name] = std::move(object_or_error.value());

extension_protocol_options_(THROW_OR_RETURN_VALUE(
    parseExtensionProtocolOptions(config, factory_context), ProtocolOptionsHashMap));
```

## Reading Code

- `RETURN_IF_NOT_OK(x)` — Early return on error.
- `THROW_OR_RETURN_VALUE(x, T)` — Unwrap StatusOr; throw on error. Used in ctors where returning Status is awkward.
