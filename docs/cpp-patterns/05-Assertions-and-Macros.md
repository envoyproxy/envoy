# Assertions and Macros

## Pattern

Envoy uses several assertion macros (from `source/common/common/assert.h`).

## Macros

| Macro | When | Behavior |
|-------|------|----------|
| `ASSERT(cond)` | Debug builds | Abort if false. Compiled out in release (unless ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE). |
| `ASSERT(cond, "msg")` | Debug | Same, with message. |
| `RELEASE_ASSERT(cond, "msg")` | Always | Abort if false. Use for invariants that must hold in production. |
| `ENVOY_BUG(cond, "msg")` | Always | If false: log, increment stat (exponential backoff), optionally abort. For "should never happen" paths. |
| `PANIC("msg")` | Always | Log and abort. |
| `SLOW_ASSERT(cond)` | Like ASSERT | For expensive checks; may be compiled out in fast release. |

## Examples (from source)

```cpp
RELEASE_ASSERT(foo == bar, "reason foo should actually be bar");
ASSERT(dispatcher == nullptr || dispatcher->isThreadSafe());
ENVOY_BUG(false, "unexpected code path");
```

## Other Macros (from source/common/common/macros.h)

| Macro | Purpose |
|-------|---------|
| `CONSTRUCT_ON_FIRST_USE(type, ...)` | Lazy static init; avoids static init order fiasco. |
| `FALLTHRU` | Mark fall-through in switch ([[fallthrough]]). |
| `ARRAY_SIZE(X)` | `sizeof(X)/sizeof(X[0])`. |

## Reading Code

- `ASSERT` — Developer assumption; may not run in release.
- `RELEASE_ASSERT` — Critical invariant; will crash if violated.
- `ENVOY_BUG` — Unexpected path; logged with backoff to avoid log flood.
