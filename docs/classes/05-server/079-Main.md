# Part 79: Main

**File:** `envoy/server/configuration.h`  
**Namespace:** `Envoy::Server::Configuration`

## Summary

`Main` is the interface for main server configuration. It provides cluster manager, stats config, and watchdog config. Implemented by `MainImpl`.

## UML Diagram

```mermaid
classDiagram
    class Main {
        <<interface>>
        +clusterManager() ClusterManager
        +statsConfig() StatsConfig
        +mainThreadWatchdogConfig() Watchdog
        +workerWatchdogConfig() Watchdog
    }
    class MainImpl {
        +implements
    }
    MainImpl ..|> Main : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `clusterManager()` | Returns cluster manager. |
| `statsConfig()` | Returns stats config. |
| `mainThreadWatchdogConfig()` | Returns main watchdog config. |
