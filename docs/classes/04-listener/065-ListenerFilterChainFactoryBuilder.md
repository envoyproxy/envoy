# Part 65: ListenerFilterChainFactoryBuilder

**File:** `source/common/listener_manager/listener_manager_impl.h`  
**Namespace:** `Envoy::Server`

## Summary

`ListenerFilterChainFactoryBuilder` builds listener filter chains from config. It implements `FilterChainFactoryBuilder` and creates the listener filter chain for new connections.

## UML Diagram

```mermaid
classDiagram
    class ListenerFilterChainFactoryBuilder {
        +buildFilterChain(callbacks) void
    }
    class FilterChainFactoryBuilder {
        <<interface>>
    }
    ListenerFilterChainFactoryBuilder ..|> FilterChainFactoryBuilder : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `buildFilterChain(callbacks)` | Builds listener filter chain. |
