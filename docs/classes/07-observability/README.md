# Observability Classes

This folder documents Envoy's observability subsystems: **Tracing**, **Access Logs**, and **Stats**. All classes are verified against the source code.

## Source Paths

| Subsystem | Interface | Implementation |
|-----------|-----------|----------------|
| Tracing | `envoy/tracing/` | `source/common/tracing/`, `source/extensions/tracers/` |
| Access Logs | `envoy/access_log/` | `source/common/access_log/`, `source/extensions/access_loggers/` |
| Stats | `envoy/stats/` | `source/common/stats/` |

## Key Documents

- [101-Tracing](101-Tracing.md) — Tracer, Span, Driver
- [102-AccessLog](102-AccessLog.md) — AccessLog::Instance, Filter, AccessLogManager
- [103-Stats](103-Stats.md) — Store, Counter, Gauge, Histogram, SymbolTable
