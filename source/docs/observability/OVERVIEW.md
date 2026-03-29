# Envoy Observability & Debugging - Overview

This document provides a high-level overview of Envoy's observability and debugging subsystems. For detailed information on specific components, see the individual documents in this directory.

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Key Components](#key-components)
4. [Request Lifecycle Integration](#request-lifecycle-integration)
5. [Related Documents](#related-documents)

---

## Overview

Envoy provides comprehensive observability through four main subsystems:

1. **Stats Subsystem** - Counters, gauges, histograms for metrics
2. **Admin Interface** - HTTP endpoints for runtime introspection
3. **Tracing Integration** - Distributed tracing support
4. **Access Logs** - Structured request/response logging

These systems work together to provide complete visibility into Envoy's runtime behavior, configuration, and request processing.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      HTTP Request Flow                       │
└─────────────────────────────────────────────────────────────┘
                             ↓
        ┌────────────────────┼────────────────────┐
        │                    │                    │
        ▼                    ▼                    ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│    Stats     │    │   Tracing    │    │ Access Logs  │
│  Subsystem   │    │  Subsystem   │    │  Subsystem   │
└──────────────┘    └──────────────┘    └──────────────┘
        │                    │                    │
        └────────────────────┼────────────────────┘
                             │
                             ▼
                    ┌──────────────┐
                    │    Admin     │
                    │  Interface   │
                    └──────────────┘
```

### Key Design Principles

1. **Minimal Performance Impact**: Thread-local storage and lock-free operations
2. **Extensibility**: Plugin-based architecture for custom stats, tracers, and loggers
3. **Runtime Introspection**: Live queries without restarts
4. **Standards Support**: Prometheus, OpenTelemetry, gRPC, etc.

---

## Key Components

### 1. Stats Subsystem

**Location**: `envoy/stats/`, `source/common/stats/`

**Purpose**: Low-overhead metrics collection and aggregation

**Key Classes**:
- `Stats::Counter` - Monotonically increasing counters
- `Stats::Gauge` - Values that can go up and down
- `Stats::Histogram` - Value distributions with quantiles
- `Stats::ThreadLocalStoreImpl` - Thread-local stats storage
- `Stats::SymbolTable` - Memory-efficient stat name encoding

**Access Points**:
- Admin endpoint: `/stats`, `/stats/prometheus`
- Stats sinks: File, gRPC, statsd, DogStatsD

### 2. Admin Interface

**Location**: `envoy/server/admin.h`, `source/server/admin/`

**Purpose**: HTTP interface for runtime debugging and introspection

**Key Endpoints**:
- `/stats` - All statistics
- `/config_dump` - Current configuration
- `/clusters` - Upstream cluster health
- `/listeners` - Active listeners
- `/certs` - TLS certificates
- `/memory` - Memory usage
- `/ready` - Readiness probe

**Architecture**: HTTP server with URL prefix matching and streaming support

### 3. Tracing Integration

**Location**: `envoy/tracing/`, `source/common/tracing/`

**Purpose**: Distributed tracing for request correlation

**Key Classes**:
- `Tracing::Tracer` - Main tracer interface
- `Tracing::Span` - Individual trace span
- `Tracing::HttpTracerUtility` - HTTP-specific tracing helpers
- `Tracing::Driver` - Backend-specific driver (Zipkin, Jaeger, etc.)

**Integration Points**:
- HTTP connection manager (span creation)
- Filter chain (span tagging)
- Upstream requests (context propagation)

### 4. Access Logs

**Location**: `envoy/access_log/`, `source/common/access_log/`

**Purpose**: Structured logging of requests and responses

**Key Classes**:
- `AccessLog::Instance` - Log writer interface
- `AccessLog::Filter` - Conditional logging
- `AccessLog::AccessLogManager` - Log file management
- `Formatter::FormatterImpl` - Log format parser

**Formats**:
- Text with format strings
- JSON structured logs
- Typed (protobuf) logs via gRPC

---

## Request Lifecycle Integration

### Phase 1: Request Arrival
```
Connection accepted
    ↓
Stats: downstream_cx_total.inc()
    ↓
Tracing: Extract trace context from headers
    ↓
Tracing: Create span with startSpan()
```

### Phase 2: Request Processing
```
Route selection
    ↓
Stats: <route_name>.request_total.inc()
    ↓
Filter chain execution
    ↓
Tracing: Tag span with request metadata
Stats: Per-filter stats updated
```

### Phase 3: Upstream Request
```
Upstream connection
    ↓
Stats: upstream_cx_total.inc()
    ↓
Tracing: Create child span for upstream
Tracing: Inject trace headers
```

### Phase 4: Response Processing
```
Response received
    ↓
Stats: Record latency histogram
Tracing: Tag span with response code
    ↓
Response sent to downstream
```

### Phase 5: Request Finalization
```
Request complete
    ↓
Tracing: Finalize and export span
    ↓
Access Logs: Write log entry
    ↓
Stats: Counters latched for export
```

### Continuous: Admin Queries

At any point during runtime:
```
Admin request → /stats
    ↓
ThreadLocalStore: Merge TLS stats
    ↓
Render stats in requested format
    ↓
Return response
```

---

## Threading Model

### Stats: Thread-Local Recording
```
Worker Thread 1          Worker Thread 2          Main Thread
     ↓                        ↓                        ↓
[TLS Counter 1]         [TLS Counter 2]         [Parent Counter]
increment()             increment()                   ↓
     ↓                        ↓                   Periodic merge
Lock-free!              Lock-free!               with lock
     ↓                        ↓                        ↓
                    Aggregated value for export
```

### Tracing: Thread-Local Spans
```
Worker Thread                              Trace Backend
     ↓                                           ↓
Create span (thread-local)
Add tags (thread-local)
     ↓                                           ↓
Finalize span → Queue for export
                         ↓                       ↓
                    Background thread exports spans
```

### Access Logs: Async Buffering
```
Worker Thread                    Logger Thread
     ↓                                 ↓
Format log entry
     ↓                                 ↓
Add to buffer
     ↓                                 ↓
                            Flush buffer to disk/network
```

---

## Performance Characteristics

### Stats
- **Recording**: O(1), lock-free in thread-local path
- **Merging**: O(N) stats, done periodically in main thread
- **Memory**: ~60-100 bytes per stat (with TLS overhead)

### Tracing
- **Overhead**: Configurable sampling rate (typically 1-10%)
- **Storage**: Spans buffered in memory before export
- **Network**: Async export to trace backends

### Access Logs
- **I/O**: Asynchronous writes, configurable buffering
- **Format**: Text parsing done once at startup
- **Rotation**: Support for USR1 signal to reopen files

### Admin
- **Impact**: Queries block serving thread briefly
- **Cost**: Proportional to number of stats/config size
- **Safety**: Read-only by default, mutations require POST

---

## Configuration Examples

### Stats Configuration
```yaml
stats_sinks:
  - name: envoy.stat_sinks.statsd
    typed_config:
      "@type": type.googleapis.com/envoy.config.metrics.v3.StatsdSink
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 8125
```

### Tracing Configuration
```yaml
tracing:
  http:
    name: envoy.tracers.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v2/spans"
      collector_endpoint_version: HTTP_JSON
```

### Access Log Configuration
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      format: "[%START_TIME%] %REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\n"
```

---

## Debugging Workflows

### Investigating High Latency

1. Check `/stats` for latency histogram percentiles
2. Enable tracing on affected routes
3. Review access logs for slow requests
4. Use `/clusters` to check upstream health
5. Check `/memory` for resource constraints

### Diagnosing Configuration Issues

1. Use `/config_dump` to verify active config
2. Check `/clusters` for endpoint resolution
3. Review `/listeners` for filter chains
4. Use `/stats` with `filter=` to find relevant counters
5. Check error counters (e.g., `*.upstream_rq_5xx`)

### Monitoring Production

1. Export `/stats/prometheus` to monitoring system
2. Configure access logs for audit trail
3. Set tracing sample rate for request tracking
4. Set up alerts on key metrics (error rates, latency)
5. Use `/ready` for health checks

---

## Common Stat Patterns

### Connection Stats
```
cluster.<name>.upstream_cx_total          # Total connections
cluster.<name>.upstream_cx_active         # Active connections
cluster.<name>.upstream_cx_connect_fail   # Connection failures
```

### Request Stats
```
cluster.<name>.upstream_rq_total          # Total requests
cluster.<name>.upstream_rq_2xx            # 2xx responses
cluster.<name>.upstream_rq_5xx            # 5xx responses
cluster.<name>.upstream_rq_time           # Request latency histogram
```

### Listener Stats
```
listener.<address>.downstream_cx_total    # Total connections
listener.<address>.downstream_cx_active   # Active connections
http.<stat_prefix>.downstream_rq_total    # Total HTTP requests
```

---

## Related Documents

- [stats_subsystem.md](./stats_subsystem.md) - Detailed stats architecture
- [admin_interface.md](./admin_interface.md) - Admin endpoints and usage
- [tracing_integration.md](./tracing_integration.md) - Distributed tracing
- [access_logs.md](./access_logs.md) - Access log configuration
- [../stats.md](../stats.md) - Original stats documentation

## Key Source Files

### Stats
- `envoy/stats/stats.h` - Core interfaces
- `source/common/stats/thread_local_store.h` - TLS implementation
- `source/common/stats/symbol_table.h` - Memory-efficient encoding

### Admin
- `envoy/server/admin.h` - Admin interface
- `source/server/admin/admin.cc` - Endpoint registration
- `source/server/admin/stats_handler.cc` - Stats endpoint

### Tracing
- `envoy/tracing/tracer.h` - Tracer interface
- `source/common/tracing/http_tracer_impl.h` - HTTP integration
- `source/common/http/conn_manager_impl.cc` - Span lifecycle

### Access Logs
- `envoy/access_log/access_log.h` - Logger interface
- `source/common/access_log/access_log_impl.cc` - Implementation
- `source/common/formatter/substitution_formatter.cc` - Format parsing
