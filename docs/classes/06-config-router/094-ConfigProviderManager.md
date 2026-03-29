# Part 94: ConfigProviderManager

**File:** `envoy/config/config_provider_manager.h`  
**Namespace:** `Envoy::Config`

## Summary

`ConfigProviderManager` creates static and dynamic (xDS) config providers. It implements `Singleton::Instance` and manages shared subscriptions. Used for RDS, CDS, etc.

## UML Diagram

```mermaid
classDiagram
    class ConfigProviderManager {
        <<interface>>
        +createXdsConfigProvider(...) ConfigProviderPtr
        +createStaticConfigProvider(...) ConfigProviderPtr
    }
    class SingletonInstance {
        <<interface>>
    }
    ConfigProviderManager ..|> SingletonInstance : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `createXdsConfigProvider(...)` | Creates xDS config provider. |
| `createStaticConfigProvider(...)` | Creates static config provider. |
