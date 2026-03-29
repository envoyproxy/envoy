# Access Logs - Detailed Documentation

## Overview

Access logs provide structured logging of HTTP requests and responses. They capture detailed information about each request including headers, status codes, latency, and custom metadata. Access logs are essential for audit trails, debugging, and analytics.

## Table of Contents

1. [Architecture](#architecture)
2. [Log Formats](#log-formats)
3. [Access Log Types](#access-log-types)
4. [Format Strings](#format-strings)
5. [Filters](#filters)
6. [Performance](#performance)
7. [Examples](#examples)

---

## Architecture

### Component Hierarchy

```
┌────────────────────────────────────────────────────┐
│         HTTP Connection Manager                    │
│      (Request processing & logging)                │
└─────────────┬──────────────────────────────────────┘
              │
              │ Calls at request end
              ▼
┌────────────────────────────────────────────────────┐
│           AccessLog::Instance                      │
│       (Logger interface per config)                │
└─────────────┬──────────────────────────────────────┘
              │
       ┌──────┴──────┬──────────────┐
       │             │              │
       ▼             ▼              ▼
┌──────────┐  ┌──────────┐  ┌─────────────┐
│  Filter  │  │  Filter  │  │   Filter    │
│(Optional)│  │(Optional)│  │  (Optional) │
└────┬─────┘  └────┬─────┘  └──────┬──────┘
     │             │                │
     │ Evaluate    │ Evaluate       │ Evaluate
     ▼             ▼                ▼
     Pass?         Pass?            Pass?
     │             │                │
     └─────────────┴────────────────┘
                   │
                   │ If all pass
                   ▼
┌────────────────────────────────────────────────────┐
│         Formatter (Parse format string)            │
└─────────────┬──────────────────────────────────────┘
              │
              │ Generate log entry
              ▼
┌────────────────────────────────────────────────────┐
│              AccessLogFile                         │
│         (Async file writer)                        │
└────────────────────────────────────────────────────┘
```

### Key Files

**Interfaces**:
- `envoy/access_log/access_log.h` - Core interfaces
- `envoy/access_log/access_log_config.h` - Config interfaces

**Implementation**:
- `source/common/access_log/access_log_impl.{h,cc}` - Base implementation
- `source/common/access_log/access_log_manager_impl.{h,cc}` - File management
- `source/common/formatter/substitution_formatter.{h,cc}` - Format parsing

**Extensions**:
- `source/extensions/access_loggers/file/` - File logger
- `source/extensions/access_loggers/grpc/` - gRPC logger
- `source/extensions/access_loggers/open_telemetry/` - OpenTelemetry logger

---

## Log Formats

### Text Format (Default)

Uses format strings with command operators.

**Example**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      format: "[%START_TIME%] \"%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%\" %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION% %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% \"%REQ(X-FORWARDED-FOR)%\" \"%REQ(USER-AGENT)%\" \"%REQ(X-REQUEST-ID)%\" \"%REQ(:AUTHORITY)%\" \"%UPSTREAM_HOST%\"\n"
```

**Sample Output**:
```
[2024-02-28T10:15:30.123Z] "GET /api/users HTTP/2" 200 - 0 1234 45 10 "10.0.0.1" "curl/7.64.0" "abc-123-def" "api.example.com" "192.168.1.10:8080"
```

---

### JSON Format

Structured JSON output for easier parsing.

**Example**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      json_format:
        time: "%START_TIME%"
        method: "%REQ(:METHOD)%"
        path: "%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%"
        protocol: "%PROTOCOL%"
        response_code: "%RESPONSE_CODE%"
        response_flags: "%RESPONSE_FLAGS%"
        bytes_received: "%BYTES_RECEIVED%"
        bytes_sent: "%BYTES_SENT%"
        duration: "%DURATION%"
        upstream_service_time: "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)%"
        x_forwarded_for: "%REQ(X-FORWARDED-FOR)%"
        user_agent: "%REQ(USER-AGENT)%"
        request_id: "%REQ(X-REQUEST-ID)%"
        authority: "%REQ(:AUTHORITY)%"
        upstream_host: "%UPSTREAM_HOST%"
```

**Sample Output**:
```json
{
  "time": "2024-02-28T10:15:30.123Z",
  "method": "GET",
  "path": "/api/users",
  "protocol": "HTTP/2",
  "response_code": 200,
  "response_flags": "-",
  "bytes_received": 0,
  "bytes_sent": 1234,
  "duration": 45,
  "upstream_service_time": "10",
  "x_forwarded_for": "10.0.0.1",
  "user_agent": "curl/7.64.0",
  "request_id": "abc-123-def",
  "authority": "api.example.com",
  "upstream_host": "192.168.1.10:8080"
}
```

---

### Typed Format (Protobuf)

Full protobuf messages for maximum detail.

**Example**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      typed_json_format:
        "@type": type.googleapis.com/envoy.data.accesslog.v3.HTTPAccessLogEntry
```

**Sample Output**:
```json
{
  "@type": "type.googleapis.com/envoy.data.accesslog.v3.HTTPAccessLogEntry",
  "common_properties": {
    "downstream_remote_address": {
      "socket_address": {
        "address": "10.0.0.1",
        "port_value": 54321
      }
    },
    "start_time": "2024-02-28T10:15:30.123Z",
    "time_to_last_rx_byte": "0.001s",
    "time_to_first_upstream_tx_byte": "0.005s",
    "time_to_last_upstream_tx_byte": "0.010s",
    "time_to_first_upstream_rx_byte": "0.020s",
    "time_to_last_upstream_rx_byte": "0.040s",
    "time_to_first_downstream_tx_byte": "0.042s",
    "time_to_last_downstream_tx_byte": "0.045s"
  },
  "request": {
    "request_method": "GET",
    "path": "/api/users",
    "authority": "api.example.com",
    "request_headers": {
      ":method": "GET",
      ":path": "/api/users",
      ":authority": "api.example.com",
      "user-agent": "curl/7.64.0"
    }
  },
  "response": {
    "response_code": 200,
    "response_headers": {
      "content-type": "application/json",
      "content-length": "1234"
    }
  }
}
```

---

## Access Log Types

### File Access Log

**Config**: `envoy.extensions.access_loggers.file.v3.FileAccessLog`

**Features**:
- Async writes to disk
- Automatic flushing
- Log rotation support (via signals)
- Multiple formats (text, JSON, typed)

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      format: "[%START_TIME%] %REQ(:METHOD)% %REQ(:PATH)% %PROTOCOL%\n"
```

**Log Rotation**:
```bash
# Send USR1 signal to reopen log files
kill -USR1 $(pgrep envoy)

# Typical logrotate config
/var/log/envoy/*.log {
    daily
    rotate 7
    compress
    delaycompress
    notifempty
    sharedscripts
    postrotate
        kill -USR1 $(cat /var/run/envoy.pid)
    endscript
}
```

---

### gRPC Access Log

**Config**: `envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig`

**Features**:
- Stream logs to remote service
- Buffering and batching
- Automatic retry
- Full protobuf messages

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.http_grpc
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig
      common_config:
        log_name: http_access_log
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: access_log_cluster
        buffer_size_bytes: 16384
        buffer_flush_interval: 1s
      additional_request_headers_to_log:
        - x-user-id
        - x-trace-id
      additional_response_headers_to_log:
        - x-cache-status
```

**Server Interface**:
```protobuf
service AccessLogService {
  rpc StreamAccessLogs(stream StreamAccessLogsMessage)
      returns (StreamAccessLogsResponse);
}
```

---

### OpenTelemetry Access Log

**Config**: `envoy.extensions.access_loggers.open_telemetry.v3.OpenTelemetryAccessLogConfig`

**Features**:
- OTLP protocol
- Integration with OpenTelemetry collectors
- Structured log records

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.open_telemetry
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.open_telemetry.v3.OpenTelemetryAccessLogConfig
      common_config:
        log_name: otel_access_log
        transport_api_version: V3
        grpc_service:
          envoy_grpc:
            cluster_name: otel_collector
      body:
        string_value: "%PROTOCOL% %REQ(:METHOD)% %REQ(:PATH)%"
      attributes:
        values:
          - key: method
            value:
              string_value: "%REQ(:METHOD)%"
          - key: status
            value:
              string_value: "%RESPONSE_CODE%"
```

---

### stdout/stderr Access Log

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /dev/stdout
      json_format:
        time: "%START_TIME%"
        method: "%REQ(:METHOD)%"
        path: "%REQ(:PATH)%"
```

---

## Format Strings

### Command Operators

#### Request Info

- `%REQ(HEADER)%` - Request header value
  - Example: `%REQ(USER-AGENT)%` → `curl/7.64.0`
  - With default: `%REQ(X-CUSTOM?DEFAULT)%` → `DEFAULT` if not present

- `%REQ(:METHOD)%` - HTTP method (GET, POST, etc.)
- `%REQ(:PATH)%` - Request path
- `%REQ(:AUTHORITY)%` - Host header
- `%REQ(X-ENVOY-ORIGINAL-PATH?:PATH)%` - Original path (before rewrites)

#### Response Info

- `%RESP(HEADER)%` - Response header value
  - Example: `%RESP(CONTENT-TYPE)%` → `application/json`

- `%RESPONSE_CODE%` - HTTP status code (200, 404, etc.)
- `%RESPONSE_CODE_DETAILS%` - Detailed response code reason
- `%RESPONSE_FLAGS%` - Response flags (see below)

#### Timing

- `%START_TIME%` - Request start time (ISO 8601)
  - Format specifier: `%START_TIME(%Y/%m/%d)%` → `2024/02/28`

- `%DURATION%` - Total duration in milliseconds
- `%REQUEST_DURATION%` - Time to receive request
- `%RESPONSE_DURATION%` - Time to send response
- `%RESPONSE_TX_DURATION%` - Time to transmit response body

#### Connection Info

- `%DOWNSTREAM_REMOTE_ADDRESS%` - Client IP:port
- `%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%` - Client IP only
- `%DOWNSTREAM_LOCAL_ADDRESS%` - Local IP:port
- `%DOWNSTREAM_PEER_CERT_V_START%` - Client cert validity start
- `%DOWNSTREAM_PEER_CERT_V_END%` - Client cert validity end
- `%DOWNSTREAM_TLS_VERSION%` - TLS version (TLSv1.2, TLSv1.3)
- `%DOWNSTREAM_TLS_CIPHER%` - Cipher suite

#### Upstream Info

- `%UPSTREAM_HOST%` - Upstream host IP:port
- `%UPSTREAM_CLUSTER%` - Cluster name
- `%UPSTREAM_LOCAL_ADDRESS%` - Local address to upstream
- `%UPSTREAM_TRANSPORT_FAILURE_REASON%` - Connection failure reason

#### Protocol

- `%PROTOCOL%` - HTTP protocol (HTTP/1.0, HTTP/1.1, HTTP/2, HTTP/3)

#### Bytes

- `%BYTES_RECEIVED%` - Request body bytes
- `%BYTES_SENT%` - Response body bytes

#### Route Info

- `%ROUTE_NAME%` - Matched route name
- `%VIRTUAL_CLUSTER_NAME%` - Virtual cluster name

#### Filter State

- `%FILTER_STATE(KEY)%` - Filter state value
  - Example: `%FILTER_STATE(envoy.upstream.remote_address)%`

#### Dynamic Metadata

- `%DYNAMIC_METADATA(NAMESPACE:KEY)%` - Dynamic metadata value
  - Example: `%DYNAMIC_METADATA(envoy.lb:canary)%`

#### Request ID

- `%REQ(X-REQUEST-ID)%` - Unique request ID

---

### Response Flags

**Format**: String of single-character flags (or `-` if none)

| Flag | Meaning |
|------|---------|
| `UH` | No healthy upstream hosts |
| `UF` | Upstream connection failure |
| `UO` | Upstream overflow (circuit breaker) |
| `NR` | No route configured |
| `URX` | Request rejected (e.g., rate limited) |
| `NC` | Upstream cluster not found |
| `DT` | Downstream connection timeout |
| `LH` | Local service failed health check |
| `UT` | Upstream request timeout |
| `LR` | Connection local reset |
| `UR` | Upstream remote reset |
| `UC` | Upstream connection termination |
| `DI` | Request processing delayed |
| `FI` | Fault injection |
| `RL` | Rate limited |
| `UAEX` | Unauthorized external service |
| `RLSE` | Rate limit service error |
| `IH` | Invalid request (e.g., bad HTTP) |
| `SI` | Stream idle timeout |
| `DPE` | Downstream protocol error |
| `UPE` | Upstream protocol error |
| `UMSDR` | Upstream max stream duration reached |
| `OM` | Overload manager terminated request |

---

### Time Format Specifiers

**Default**: ISO 8601 format (`2024-02-28T10:15:30.123Z`)

**Custom format**:
```
%START_TIME(%Y/%m/%d %H:%M:%S)%
→ 2024/02/28 10:15:30
```

**Supported specifiers** (strftime format):
- `%Y` - Year (2024)
- `%m` - Month (01-12)
- `%d` - Day (01-31)
- `%H` - Hour (00-23)
- `%M` - Minute (00-59)
- `%S` - Second (00-59)
- `%e` - Milliseconds (000-999)
- `%f` - Microseconds (000000-999999)

---

## Filters

### Purpose

Filters control which requests are logged. Multiple filters can be combined (AND logic).

### Filter Types

#### Status Code Filter

Log only specific response codes.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      status_code_filter:
        comparison:
          op: GE  # Greater or Equal
          value:
            default_value: 400
            runtime_key: access_log.status_filter
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/errors.log
```

**Operators**: `EQ`, `GE`, `LE`

---

#### Duration Filter

Log only slow requests.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      duration_filter:
        comparison:
          op: GE
          value:
            default_value: 1000  # 1 second
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/slow.log
```

---

#### Response Flag Filter

Log only requests with specific flags.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      response_flag_filter:
        flags:
          - UF  # Upstream connection failure
          - UH  # No healthy upstream
          - UT  # Upstream request timeout
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/upstream_errors.log
```

---

#### Header Filter

Log based on header presence/value.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      header_filter:
        header:
          name: x-debug
          string_match:
            exact: "true"
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/debug.log
```

---

#### Runtime Filter

Control logging dynamically via runtime.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      runtime_filter:
        runtime_key: access_log.sample_rate
        percent_sampled:
          numerator: 10
          denominator: HUNDRED  # 10% sampling
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/sampled.log
```

---

#### And Filter

Combine multiple filters (all must pass).

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      and_filter:
        filters:
          - status_code_filter:
              comparison:
                op: GE
                value:
                  default_value: 500
          - duration_filter:
              comparison:
                op: GE
                value:
                  default_value: 100
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/slow_errors.log
```

---

#### Or Filter

Any filter can pass.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      or_filter:
        filters:
          - status_code_filter:
              comparison:
                op: GE
                value:
                  default_value: 400
          - duration_filter:
              comparison:
                op: GE
                value:
                  default_value: 1000
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/errors_or_slow.log
```

---

#### Not Filter

Invert filter result.

**Configuration**:
```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      not_filter:
        filter:
          status_code_filter:
            comparison:
              op: EQ
              value:
                default_value: 200
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/non_200.log
```

---

## Performance

### Write Performance

**File Access Logs**:
- Async writes (non-blocking)
- Buffered I/O
- Typical overhead: ~10-50 microseconds per log

**gRPC Access Logs**:
- Batched sends
- Configurable buffer size
- Network overhead depends on collector

### Memory Usage

**Per request**:
- Log entry formatting: ~1-2 KB
- Buffer until flush: Depends on buffer size

**At scale** (100k req/s):
- With full logging: ~100-200 MB/s disk writes
- With 10% sampling: ~10-20 MB/s

### Optimization Tips

1. **Use filters** to reduce volume
   ```yaml
   filter:
     runtime_filter:
       percent_sampled:
         numerator: 10  # 10% sampling
   ```

2. **Avoid expensive operators**
   - `%DYNAMIC_METADATA%` requires metadata lookup
   - `%FILTER_STATE%` requires state lookup

3. **Use structured formats**
   - JSON parsing is faster than complex text formats

4. **Separate logs by priority**
   ```yaml
   # Errors only
   access_log:
     - filter:
         status_code_filter:
           comparison:
             op: GE
             value:
               default_value: 400
       typed_config:
         path: /var/log/envoy/errors.log

   # Sample success requests
   access_log:
     - filter:
         and_filter:
           filters:
             - status_code_filter:
                 comparison:
                   op: EQ
                   value:
                     default_value: 200
             - runtime_filter:
                 percent_sampled:
                   numerator: 1  # 1% of 200s
       typed_config:
         path: /var/log/envoy/success_sampled.log
   ```

5. **Configure buffer sizes**
   ```yaml
   typed_config:
     "@type": type.googleapis.com/envoy.extensions.access_loggers.grpc.v3.HttpGrpcAccessLogConfig
     common_config:
       buffer_size_bytes: 65536  # Larger buffer
       buffer_flush_interval: 5s  # Less frequent flushes
   ```

---

## Examples

### Apache-style Combined Log

```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/access.log
      format: "%DOWNSTREAM_REMOTE_ADDRESS% - - [%START_TIME%] \"%REQ(:METHOD)% %REQ(:PATH)% %PROTOCOL%\" %RESPONSE_CODE% %BYTES_SENT% \"%REQ(REFERER)%\" \"%REQ(USER-AGENT)%\"\n"
```

**Output**:
```
10.0.0.1 - - [2024-02-28T10:15:30.123Z] "GET /api/users HTTP/2" 200 1234 "https://example.com/" "Mozilla/5.0"
```

---

### Detailed Debug Log

```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/debug.log
      json_format:
        time: "%START_TIME%"
        request_id: "%REQ(X-REQUEST-ID)%"
        method: "%REQ(:METHOD)%"
        path: "%REQ(:PATH)%"
        query: "%REQ(:PATH)%"
        authority: "%REQ(:AUTHORITY)%"
        protocol: "%PROTOCOL%"
        response_code: "%RESPONSE_CODE%"
        response_flags: "%RESPONSE_FLAGS%"
        response_code_details: "%RESPONSE_CODE_DETAILS%"
        bytes_received: "%BYTES_RECEIVED%"
        bytes_sent: "%BYTES_SENT%"
        duration: "%DURATION%"
        upstream_service_time: "%RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)%"
        upstream_host: "%UPSTREAM_HOST%"
        upstream_cluster: "%UPSTREAM_CLUSTER%"
        upstream_local_address: "%UPSTREAM_LOCAL_ADDRESS%"
        downstream_local_address: "%DOWNSTREAM_LOCAL_ADDRESS%"
        downstream_remote_address: "%DOWNSTREAM_REMOTE_ADDRESS%"
        user_agent: "%REQ(USER-AGENT)%"
        x_forwarded_for: "%REQ(X-FORWARDED-FOR)%"
        route_name: "%ROUTE_NAME%"
```

---

### Error-Only Log

```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      status_code_filter:
        comparison:
          op: GE
          value:
            default_value: 400
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/errors.log
      json_format:
        time: "%START_TIME%"
        error_type: "http_error"
        status: "%RESPONSE_CODE%"
        details: "%RESPONSE_CODE_DETAILS%"
        flags: "%RESPONSE_FLAGS%"
        method: "%REQ(:METHOD)%"
        path: "%REQ(:PATH)%"
        client: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
```

---

### Upstream Failure Log

```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      response_flag_filter:
        flags:
          - UF
          - UH
          - UC
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/upstream_failures.log
      format: "[%START_TIME%] UPSTREAM_FAILURE cluster=%UPSTREAM_CLUSTER% host=%UPSTREAM_HOST% flags=%RESPONSE_FLAGS% path=%REQ(:PATH)%\n"
```

---

### Audit Log (Sensitive Operations)

```yaml
access_log:
  - name: envoy.access_loggers.file
    filter:
      and_filter:
        filters:
          - header_filter:
              header:
                name: ":path"
                string_match:
                  prefix: "/api/admin"
          - or_filter:
              filters:
                - header_filter:
                    header:
                      name: ":method"
                      string_match:
                        exact: "POST"
                - header_filter:
                    header:
                      name: ":method"
                      string_match:
                        exact: "DELETE"
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /var/log/envoy/audit.log
      json_format:
        timestamp: "%START_TIME%"
        audit_event: "api_admin_mutation"
        user: "%REQ(X-USER-ID)%"
        method: "%REQ(:METHOD)%"
        path: "%REQ(:PATH)%"
        client_ip: "%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%"
        status: "%RESPONSE_CODE%"
```

---

## Debugging

### Enable Access Log Debug Logging

```bash
curl -X POST 'http://localhost:9901/logging?access_log=debug'
```

### Check Access Log Stats

```bash
curl http://localhost:9901/stats | grep access_log
```

**Stats**:
- `access_log.<log_name>.logged` - Number of logs written
- `access_log.<log_name>.filtered` - Number filtered out

### Test Format String

Create test access log with verbose format:

```yaml
access_log:
  - name: envoy.access_loggers.file
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /dev/stdout
      json_format:
        __all_vars__: "Test all available vars"
        start_time: "%START_TIME%"
        duration: "%DURATION%"
        protocol: "%PROTOCOL%"
        # ... add all vars you want to test
```

### Common Issues

**Log file not created**:
- Check directory permissions
- Verify path is writable
- Check Envoy logs for errors

**Missing values (shows as `-`)**:
- Header doesn't exist
- Use default value: `%REQ(X-CUSTOM?DEFAULT)%`

**Slow log writes**:
- Check disk I/O: `iostat -x 1`
- Reduce log volume with filters
- Use faster storage (SSD)

---

## See Also

- [OVERVIEW.md](./OVERVIEW.md) - Observability overview
- [stats_subsystem.md](./stats_subsystem.md) - Stats details
- `source/docs/logging.md` - General logging documentation
- `envoy/access_log/access_log.h` - Access log interfaces
