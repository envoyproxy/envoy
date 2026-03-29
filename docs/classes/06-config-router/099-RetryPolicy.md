# Part 99: RetryPolicy

**File:** `envoy/router/router.h`  
**Namespace:** `Envoy::Router`

## Summary

`RetryPolicy` defines retry behavior: retry-on conditions, num retries, per-try timeout, retry host predicate. Used by `RouteEntry` for upstream retries.

## UML Diagram

```mermaid
classDiagram
    class RetryPolicy {
        +numRetries() uint32_t
        +retryOn() string
        +perTryTimeout() optional
        +retryHostPredicate() HostPredicate
    }
    class RouteEntry {
        +retryPolicy() RetryPolicy
    }
    RouteEntry ..> RetryPolicy : returns
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `numRetries()` | Returns max retries. |
| `retryOn()` | Returns retry conditions. |
| `perTryTimeout()` | Returns per-try timeout. |
