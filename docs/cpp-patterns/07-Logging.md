# Logging

## Pattern

Envoy uses a custom logging system with logger IDs. See `source/common/common/logger.h`.

## Logger::Loggable

Classes that log inherit `Logger::Loggable<Logger::Id::xxx>`:

```cpp
class ConnectionImpl : public ConnectionImplBase,
                       protected Logger::Loggable<Logger::Id::connection> {
  // Can use ENVOY_LOG in this class
};
```

## ENVOY_LOG

```cpp
ENVOY_LOG(debug, "message");
ENVOY_LOG(info, "key: {}", value);
ENVOY_LOG_TO_LOGGER(logger, error, "message");
```

Levels: `trace`, `debug`, `info`, `warn`, `error`, `critical`.

## Logger IDs (from ALL_LOGGER_IDS in logger.h)

Examples: `connection`, `http`, `config`, `router`, `upstream`, `client`, `filter`, etc.

Each ID has a separate log; can filter by ID for debugging.

## Reading Code

- `Logger::Loggable<Logger::Id::X>` — This class logs under ID X.
- `ENVOY_LOG(level, fmt, ...)` — Uses spdlog-style `{}` placeholders.
