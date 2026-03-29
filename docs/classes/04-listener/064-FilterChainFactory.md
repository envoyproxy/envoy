# Part 64: FilterChainFactory

**File:** `envoy/network/filter.h`  
**Namespace:** `Envoy::Network`

## Summary

`FilterChainFactory` creates filter chains for new connections. It provides `createListenerFilterChain` and `createNetworkFilterChain` to set up listener and network filters on a connection.

## UML Diagram

```mermaid
classDiagram
    class FilterChainFactory {
        <<interface>>
        +createListenerFilterChain(callbacks) bool
        +createNetworkFilterChain(connection, filters) bool
    }
    class ListenerImpl {
        +implements
    }
    ListenerImpl ..|> FilterChainFactory : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `createListenerFilterChain(callbacks)` | Creates listener filter chain. |
| `createNetworkFilterChain(connection, filters)` | Creates network filter chain. |
