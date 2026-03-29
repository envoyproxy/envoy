# Part 80: Instance

**File:** `envoy/server/instance.h`  
**Namespace:** `Envoy::Server`

## Summary

`Instance` is the interface for the Envoy server instance. It provides cluster manager, listener manager, admin, runtime, and other core components. Implemented by `InstanceImpl`.

## UML Diagram

```mermaid
classDiagram
    class Instance {
        <<interface>>
        +clusterManager() ClusterManager
        +listenerManager() ListenerManager
        +admin() Admin
        +runtime() Runtime
    }
    class InstanceImpl {
        +implements
    }
    InstanceImpl ..|> Instance : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `clusterManager()` | Returns cluster manager. |
| `listenerManager()` | Returns listener manager. |
| `admin()` | Returns admin. |
| `runtime()` | Returns runtime. |
