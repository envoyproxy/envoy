# Tracing Integration - Detailed Documentation

## Overview

Envoy's tracing subsystem provides distributed tracing support for correlating requests across multiple services. It integrates with popular tracing backends like Zipkin, Jaeger, Datadog, and OpenTelemetry.

## Table of Contents

1. [Architecture](#architecture)
2. [Core Concepts](#core-concepts)
3. [Request Lifecycle](#request-lifecycle)
4. [Tracing Backends](#tracing-backends)
5. [Configuration](#configuration)
6. [Custom Tags](#custom-tags)
7. [Performance](#performance)

---

## Architecture

### Component Hierarchy

```
┌────────────────────────────────────────────────────────┐
│            HTTP Connection Manager                     │
│         (Request processing entry point)               │
└────────────┬──────────────────────────────────────────┘
             │
             │ Uses
             ▼
┌────────────────────────────────────────────────────────┐
│                   HttpTracer                           │
│            (HTTP-specific tracing logic)               │
└────────────┬──────────────────────────────────────────┘
             │
             │ Delegates to
             ▼
┌────────────────────────────────────────────────────────┐
│                    Tracer                              │
│            (Abstract tracer interface)                 │
└────────────┬──────────────────────────────────────────┘
             │
             │ Creates
             ▼
┌────────────────────────────────────────────────────────┐
│                     Span                               │
│         (Represents single operation)                  │
└────────────┬──────────────────────────────────────────┘
             │
             │ Uses
             ▼
┌────────────────────────────────────────────────────────┐
│                    Driver                              │
│      (Backend-specific implementation)                 │
│     (Zipkin, Jaeger, DataDog, etc.)                   │
└────────────────────────────────────────────────────────┘
```

### Key Files

**Interfaces**:
- `envoy/tracing/tracer.h` - Main tracer interface
- `envoy/tracing/trace_driver.h` - Backend driver interface
- `envoy/tracing/trace_context.h` - Context propagation

**Implementation**:
- `source/common/tracing/http_tracer_impl.h` - HTTP tracing
- `source/common/tracing/tracer_impl.h` - Tracer implementation
- `source/common/tracing/null_span_impl.h` - No-op span

**Integration**:
- `source/common/http/conn_manager_impl.cc` - Request lifecycle hooks

---

## Core Concepts

### Span

A span represents a single operation in a distributed trace.

**Interface**: `envoy/tracing/trace_driver.h:23-105`

```cpp
class Span {
public:
    // Set operation name
    virtual void setOperation(absl::string_view operation) PURE;

    // Add tag (key-value metadata)
    virtual void setTag(absl::string_view name, absl::string_view value) PURE;

    // Log event
    virtual void log(SystemTime timestamp, const std::string& event) PURE;

    // Mark span as finished
    virtual void finishSpan() PURE;

    // Inject context into headers (for propagation)
    virtual void injectContext(TraceContext& context) PURE;

    // Create child span
    virtual SpanPtr spawnChild(const Config& config, const std::string& name, SystemTime start_time) PURE;
};
```

**Lifecycle**:
```
1. Create span       → tracer->startSpan()
2. Set tags          → span->setTag("http.method", "GET")
3. Process request   → Application logic
4. Log events        → span->log("cache_hit")
5. Finalize span     → span->finishSpan()
```

---

### Trace Context

Propagates tracing information across service boundaries.

**Interface**: `envoy/tracing/trace_context.h:18-56`

```cpp
class TraceContext {
public:
    // Get header value
    virtual absl::optional<absl::string_view> get(absl::string_view key) PURE;

    // Set header value
    virtual void set(absl::string_view key, absl::string_view value) PURE;

    // Remove header
    virtual void remove(absl::string_view key) PURE;

    // Iterate all headers
    virtual void forEach(IterateCallback callback) PURE;

    // Access to underlying request headers
    virtual OptRef<Http::RequestHeaderMap> requestHeaders() PURE;
};
```

**HTTP Implementation**: `source/common/tracing/http_tracer_impl.h:59-67`

```cpp
class HttpTraceContext : public TraceContext {
    // Wraps Http::RequestHeaderMap
    // Provides convenient access to protocol, host, path, method
};
```

---

### Tracing Decision

Determines whether to trace a request.

**Enum**: `envoy/tracing/trace_reason.h`

```cpp
enum class Reason {
    NotTraceable,      // Don't trace
    Sampling,          // Random sampling
    ServiceForced,     // Service forced tracing
    ClientForced,      // Client requested tracing
};

struct Decision {
    Reason reason;
    bool traced;       // Whether to trace
};
```

**Decision Factors**:
1. **Sampling rate** - Random percentage (e.g., 1%)
2. **Client headers** - `x-envoy-force-trace`, `x-client-trace-id`
3. **Request ID** - Sampling based on request ID
4. **Route config** - Per-route tracing configuration

---

### Tracer Configuration

**Interface**: `envoy/tracing/trace_config.h`

```cpp
class Config {
public:
    // Operation name (e.g., "ingress", "egress")
    virtual const std::string& operationName() const PURE;

    // Custom tags to add to spans
    virtual const CustomTagMap& customTags() const PURE;

    // Max path length before truncation
    virtual uint32_t maxPathTagLength() const PURE;

    // Whether to trace verbose info
    virtual bool verbose() const PURE;
};
```

---

## Request Lifecycle

### Phase 1: Request Arrival

**Location**: `source/common/http/conn_manager_impl.cc` (early request processing)

```cpp
void ActiveStream::decodeHeaders(...) {
    // Extract trace context from request headers
    Tracing::HttpTraceContext trace_context(*request_headers);

    // Make tracing decision
    Tracing::Decision decision = tracingDecision();

    if (decision.traced) {
        // Create root span
        active_span_ = tracer->startSpan(
            tracing_config, trace_context, stream_info, decision
        );

        // Set initial tags
        active_span_->setTag("http.method", request_headers->getMethodValue());
        active_span_->setTag("http.url", request_headers->getPathValue());
    }
}
```

**Trace Context Extraction**:

Looks for standard trace headers:
- `x-request-id` - Envoy's request ID
- `x-trace-id` - Trace ID
- `x-span-id` - Parent span ID
- `x-b3-traceid` - B3 trace ID (Zipkin)
- `x-b3-spanid` - B3 span ID
- `traceparent` - W3C Trace Context
- `tracestate` - W3C Trace State

---

### Phase 2: Downstream Processing

**Tags Added During Processing**:

```cpp
// HTTP attributes
span->setTag("http.protocol", protocol_string);
span->setTag("http.status_code", status_code);

// Request info
span->setTag("request_size", request_size);
span->setTag("response_size", response_size);
span->setTag("request_id", request_id);

// Routing
span->setTag("upstream_cluster", cluster_name);
span->setTag("upstream_address", host_address);

// Response info
span->setTag("response_flags", response_flags);

// Custom tags from config
for (const auto& tag : custom_tags) {
    span->setTag(tag.first, tag.second->value(context));
}
```

---

### Phase 3: Upstream Request

**Creating Child Span**:

```cpp
// Create child span for upstream request
SpanPtr child_span = active_span_->spawnChild(
    config, "upstream_request", timeSource().systemTime()
);

// Inject context into upstream headers
Tracing::HttpTraceContext upstream_context(*upstream_headers);
child_span->injectContext(upstream_context);

// Now upstream service can continue the trace
```

**Context Injection**:

Adds headers to upstream request:
```
x-request-id: 1234-5678-90ab-cdef
x-trace-id: fedcba09-8765-4321
x-span-id: 0011223344556677
x-parent-span-id: 8899aabbccddeeff
```

---

### Phase 4: Response Processing

**Adding Response Tags**:

```cpp
void HttpTracerUtility::onUpstreamResponseHeaders(
    Span& span, const Http::ResponseHeaderMap* response_headers) {

    if (response_headers) {
        // Tag with response code
        span.setTag("http.status_code", std::to_string(response_headers->getStatusValue()));

        // Tag with content type
        const auto* content_type = response_headers->ContentType();
        if (content_type) {
            span.setTag("http.content_type", content_type->value().getStringView());
        }
    }
}
```

---

### Phase 5: Span Finalization

**Location**: `source/common/http/conn_manager_impl.cc:852`

```cpp
void ActiveStream::onResponseComplete() {
    if (active_span_) {
        // Add final tags and finish span
        Tracing::HttpTracerUtility::finalizeDownstreamSpan(
            *active_span_, request_headers_.get(), response_headers_.get(), response_trailers_.get(), filter_manager_.streamInfo(), *this
        );
    }
}
```

**Location**: `source/common/tracing/http_tracer_impl.cc`

```cpp
void HttpTracerUtility::finalizeDownstreamSpan(
    Span& span, const Http::RequestHeaderMap* request_headers, const Http::ResponseHeaderMap* response_headers, const Http::ResponseTrailerMap* response_trailers, const StreamInfo::StreamInfo& stream_info, const Config& tracing_config) {

    // Set common tags
    setCommonTags(span, stream_info, tracing_config, false);

    // Set HTTP specific tags
    if (request_headers) {
        span.setTag("http.method", request_headers->getMethodValue());
        span.setTag("http.url", request_headers->getPathValue());
    }

    if (response_headers) {
        span.setTag("http.status_code", std::to_string(response_headers->getStatusValue()));
    }

    // Set error tag if needed
    if (stream_info.hasResponseFlag(StreamInfo::ResponseFlag::FaultInjected)) {
        span.setTag("error", "true");
    }

    // Finish span (triggers export to backend)
    span.finishSpan();
}
```

---

## Tracing Backends

### Zipkin

**Configuration**:
```yaml
tracing:
  http:
    name: envoy.tracers.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v2/spans"
      collector_endpoint_version: HTTP_JSON
      trace_id_128bit: true
      shared_span_context: false
```

**Implementation**: `source/extensions/tracers/zipkin/`

**Format**: Zipkin v2 JSON or Thrift

**Headers**:
- `X-B3-TraceId` - Trace ID (64 or 128-bit hex)
- `X-B3-SpanId` - Span ID (64-bit hex)
- `X-B3-ParentSpanId` - Parent span ID
- `X-B3-Sampled` - Sampling decision (0 or 1)
- `X-B3-Flags` - Debug flag

---

### Jaeger

**Configuration**:
```yaml
tracing:
  http:
    name: envoy.tracers.jaeger
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.JaegerConfig
      collector_cluster: jaeger
      collector_endpoint: "/api/traces"
```

**Implementation**: `source/extensions/tracers/jaeger/`

**Format**: Jaeger Thrift

**Headers**:
- `uber-trace-id` - Format: `{trace-id}:{span-id}:{parent-id}:{flags}`
  - Example: `4bf92f3577b34da6a3ce929d0e0e4736:00f067aa0ba902b7:0:1`

---

### OpenTelemetry

**Configuration**:
```yaml
tracing:
  http:
    name: envoy.tracers.opentelemetry
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.OpenTelemetryConfig
      grpc_service:
        envoy_grpc:
          cluster_name: opentelemetry_collector
      service_name: my-service
```

**Implementation**: `source/extensions/tracers/opentelemetry/`

**Format**: OTLP (OpenTelemetry Protocol)

**Headers** (W3C Trace Context):
- `traceparent` - Format: `00-{trace-id}-{parent-id}-{flags}`
  - Example: `00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01`
- `tracestate` - Vendor-specific context

---

### Datadog

**Configuration**:
```yaml
tracing:
  http:
    name: envoy.tracers.datadog
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.DatadogConfig
      collector_cluster: datadog_agent
      service_name: my-service
```

**Implementation**: `source/extensions/tracers/datadog/`

**Format**: Datadog APM

**Headers**:
- `x-datadog-trace-id` - Trace ID (64-bit decimal)
- `x-datadog-parent-id` - Parent span ID (64-bit decimal)
- `x-datadog-sampling-priority` - Sampling decision

---

### Dynamic Tracing (DynamicOT)

Loads tracing implementations at runtime from shared libraries.

**Configuration**:
```yaml
tracing:
  http:
    name: envoy.tracers.dynamic_ot
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.DynamicOtConfig
      library: /usr/local/lib/libjaegertracing_plugin.so
      config:
        service_name: my-service
        sampler:
          type: probabilistic
          param: 0.01
```

---

## Configuration

### HTTP Connection Manager

```yaml
http_filters:
  - name: envoy.filters.network.http_connection_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
      stat_prefix: ingress
      tracing:
        # Random sampling percentage (0-100)
        random_sampling:
          value: 1.0

        # Overall percentage of requests to trace (0-100)
        overall_sampling:
          value: 100.0

        # Client-side sampling percentage
        client_sampling:
          value: 100.0

        # Operation name for spans
        operation_name: INGRESS

        # Max path tag length
        max_path_tag_length: 256

        # Spawn upstream spans
        spawn_upstream_span: true

        # Custom tags
        custom_tags:
          - tag: environment
            literal:
              value: production

          - tag: user_id
            request_header:
              name: x-user-id
              default_value: unknown

          - tag: cluster
            metadata:
              kind:
                cluster: {}
              metadata_key:
                key: envoy.lb
                path:
                  - key: canary

        # Verbose mode (more tags)
        verbose: false
```

### Global Tracing Config

```yaml
static_resources:
  clusters:
    - name: zipkin
      type: STRICT_DNS
      lb_policy: ROUND_ROBIN
      load_assignment:
        cluster_name: zipkin
        endpoints:
          - lb_endpoints:
              - endpoint:
                  address:
                    socket_address:
                      address: zipkin-collector
                      port_value: 9411

tracing:
  http:
    name: envoy.tracers.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v2/spans"
      collector_endpoint_version: HTTP_JSON
      split_spans_for_request: true
```

---

## Custom Tags

### Tag Sources

**1. Literal Value**:
```yaml
custom_tags:
  - tag: version
    literal:
      value: v1.2.3
```

**2. Request Header**:
```yaml
custom_tags:
  - tag: user_agent
    request_header:
      name: user-agent
      default_value: unknown
```

**3. Request Metadata**:
```yaml
custom_tags:
  - tag: route_name
    metadata:
      kind:
        route: {}
      metadata_key:
        key: envoy.route
        path:
          - key: name
```

**4. Environment Variable**:
```yaml
custom_tags:
  - tag: hostname
    environment:
      name: HOSTNAME
      default_value: unknown
```

---

### Programmatic Custom Tags

**Interface**: `envoy/tracing/custom_tag.h`

```cpp
class CustomTag {
public:
    // Get tag value for span
    virtual absl::string_view value(const LogContext& context) const PURE;
};
```

**Example**:
```cpp
class MyCustomTag : public Tracing::CustomTag {
public:
    absl::string_view value(const LogContext& context) const override {
        // Extract value from request context
        const auto& stream_info = context.stream_info();
        return stream_info.dynamicMetadata()
            .filter_metadata()
            .at("my_filter")
            .fields()
            .at("my_field")
            .string_value();
    }
};

// Register factory
REGISTER_FACTORY(MyCustomTagFactory, Tracing::CustomTagFactory);
```

---

## Performance

### Sampling Strategies

**Random Sampling**:
```yaml
tracing:
  random_sampling:
    value: 1.0  # 1% of requests
```

**Pros**: Unbiased sample
**Cons**: May miss important requests

**Request ID-based Sampling**:
```yaml
tracing:
  random_sampling:
    value: 1.0
  # Uses request ID for deterministic sampling
```

**Pros**: Same request always traced/not-traced
**Cons**: Still random selection

**Client-Forced Tracing**:
```bash
# Client sets header
curl -H "x-envoy-force-trace: true" http://api.example.com/
```

**Pros**: Debug specific requests
**Cons**: Can be abused

---

### Performance Impact

**Untraced Requests**:
- Overhead: ~1-2 microseconds (check sampling decision)
- Memory: Minimal (just decision logic)

**Traced Requests**:
- CPU: ~50-100 microseconds per span
- Memory: ~1-2 KB per span
- Network: Depends on export strategy (batch vs immediate)

**At Scale** (1M req/s, 1% sampling):
- Traced: 10,000 req/s
- Span creation: ~1 second CPU/s
- Memory: ~20 MB (with buffering)
- Network: ~50 Mbps (compressed)

---

### Optimization Tips

1. **Use appropriate sampling rate**
   - Production: 0.1-1%
   - Staging: 10-50%
   - Development: 100%

2. **Batch span exports**
   ```yaml
   collector_endpoint_version: HTTP_JSON
   # Most backends buffer and batch automatically
   ```

3. **Limit tag size**
   ```yaml
   max_path_tag_length: 256
   ```

4. **Disable verbose mode in production**
   ```yaml
   verbose: false
   ```

5. **Use shared span context**
   ```yaml
   shared_span_context: true  # Reduces span count
   ```

---

## Debugging

### Enable Tracing Logs

```bash
curl -X POST 'http://localhost:9901/logging?tracing=debug'
```

### Force Trace Specific Request

```bash
curl -H "x-envoy-force-trace: true" \
     -H "x-request-id: debug-trace-001" \
     http://localhost:8080/api
```

### Check Trace Export

Look for stats:
```bash
curl http://localhost:9901/stats | grep tracing
```

Stats to check:
- `tracing.zipkin.spans_sent` - Successful exports
- `tracing.zipkin.reports_sent` - Batch reports sent
- `tracing.zipkin.reports_dropped` - Failed exports

### View Span in Backend

With trace ID from logs/headers, query backend:

**Zipkin**:
```bash
curl http://zipkin:9411/api/v2/trace/{trace-id}
```

**Jaeger**:
```bash
curl http://jaeger:16686/api/traces/{trace-id}
```

---

## Common Patterns

### Microservices Tracing

```
Client Request
    ↓ (creates root span)
API Gateway (Envoy)
    ↓ (creates child span)
Service A (Envoy)
    ↓ (creates child span)
Service B (Envoy)
    ↓ (creates child span)
Database

Result: 4 spans showing complete request flow
```

### Service Mesh

```yaml
# Ingress Envoy
tracing:
  operation_name: INGRESS
  spawn_upstream_span: true

# Sidecar Envoy
tracing:
  operation_name: EGRESS
  spawn_upstream_span: false  # Avoid duplicate spans
```

### Error Tracking

```cpp
if (response_code >= 500) {
    span->setTag("error", "true");
    span->setTag("error.kind", "server_error");
    span->log(timeSource().systemTime(), "Upstream returned 5xx");
}
```

---

## See Also

- [OVERVIEW.md](./OVERVIEW.md) - Observability overview
- [access_logs.md](./access_logs.md) - Access logging
- `envoy/tracing/` - Tracing interfaces
- `source/extensions/tracers/` - Backend implementations
