# Part 68: FilterChainImpl

**File:** `source/common/listener_manager/filter_chain_manager_impl.h`  
**Namespace:** `Envoy::Server`

## Summary

`FilterChainImpl` implements `Network::DrainableFilterChain` and holds the concrete filter chain: transport socket factory, network filter factories, and metadata. Created by `FilterChainManagerImpl`.

## UML Diagram

```mermaid
classDiagram
    class FilterChainImpl {
        +transportSocketFactory() TransportSocketFactory
        +networkFilterFactories() list
        +startDraining() void
    }
    class DrainableFilterChain {
        <<interface>>
    }
    FilterChainImpl ..|> DrainableFilterChain : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `transportSocketFactory()` | Returns transport socket factory. |
| `networkFilterFactories()` | Returns network filter factories. |
| `startDraining()` | Starts draining filter chain. |
