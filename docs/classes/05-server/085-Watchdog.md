# Part 85: Watchdog

**File:** `envoy/server/configuration.h`  
**Namespace:** `Envoy::Server::Configuration`

## Summary

`Watchdog` configures thread responsiveness monitoring. It provides miss/mega-miss/kill timeouts and multi-kill thresholds. Used for main and worker watchdog config during server init.

## UML Diagram

```mermaid
classDiagram
    class Watchdog {
        <<interface>>
        +missTimeout() milliseconds
        +megaMissTimeout() milliseconds
        +killTimeout() milliseconds
        +multiKillTimeout() milliseconds
    }
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `missTimeout()` | Time for miss statistic. |
| `megaMissTimeout()` | Time for mega-miss. |
| `killTimeout()` | Time before process kill. |
| `multiKillTimeout()` | Time for multi-thread kill. |
