# Part 55: HostStats

**File:** `envoy/upstream/host_description.h`  
**Namespace:** `Envoy::Upstream`

## Summary

`HostStats` is a struct holding per-host counters and gauges (e.g. failures, active connections). Used by `HostDescription::stats()` for observability.

## UML Diagram

```mermaid
classDiagram
    class HostStats {
        +struct
        +counters
        +gauges
    }
    class HostDescription {
        +stats() HostStats
    }
    HostDescription ..> HostStats : returns
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `HostDescription::stats()` | Returns host stats. |
| `counters()` | Returns host counters. |
| `gauges()` | Returns host gauges. |
