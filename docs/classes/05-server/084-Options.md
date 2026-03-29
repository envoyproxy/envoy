# Part 84: Options

**File:** `envoy/server/options.h`  
**Namespace:** `Envoy::Server`

## Summary

`Options` is the interface for command-line and startup options. It provides config path, concurrency, log level, and other runtime options. Implemented by `OptionsImpl`.

## UML Diagram

```mermaid
classDiagram
    class Options {
        <<interface>>
        +configPath() string
        +concurrency() uint32_t
        +logLevel() string
        +componentLogLevels() string
    }
    class OptionsImpl {
        +implements
    }
    OptionsImpl ..|> Options : implements
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `configPath()` | Returns config path. |
| `concurrency()` | Returns worker count. |
| `logLevel()` | Returns log level. |
