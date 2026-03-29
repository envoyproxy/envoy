# Part 86: ConfigurationImpl (ConfigImpl)

**File:** `source/common/router/config_impl.h`  
**Namespace:** `Envoy::Router`

## Summary

`ConfigImpl` implements `Router::Config` for route configuration from RDS. It parses RouteConfiguration proto, builds route matchers, and provides route lookup. Used by `RdsRouteConfigProviderImpl`.

## UML Diagram

```mermaid
classDiagram
    class ConfigImpl {
        +route(request, stream_info) Route
        +config() RouteConfiguration
        +virtualHosts() vector
    }
    class Config {
        <<interface>>
    }
    ConfigImpl ..|> Config : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `route(request, stream_info)` | Finds route for request. |
| `config()` | Returns route config proto. |
| `virtualHosts()` | Returns virtual hosts. |
