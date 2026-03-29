# Part 50: ResourceManager

**File:** `envoy/upstream/resource_manager.h`  
**Namespace:** `Envoy::Upstream`

## Summary

`ResourceManager` enforces per-cluster resource limits (connections, pending requests). It prevents resource exhaustion and provides circuit-breaking behavior. Implemented by `ResourceManagerImpl`.

## UML Diagram

```mermaid
classDiagram
    class ResourceManager {
        <<interface>>
        +connections() Resource
        +pendingRequests() Resource
        +retries() Resource
        +maxConnections() uint64_t
    }
    class Resource {
        <<interface>>
        +inc() bool
        +dec() void
        +max() uint64_t
    }
    ResourceManager ..> Resource : contains
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `connections()` | Returns connection resource. |
| `pendingRequests()` | Returns pending request resource. |
| `retries()` | Returns retry resource. |
| `maxConnections()` | Returns max connections. |
