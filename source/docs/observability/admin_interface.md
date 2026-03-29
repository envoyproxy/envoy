# Admin Interface - Detailed Documentation

## Overview

The Admin interface provides an HTTP server for runtime introspection and debugging of Envoy. It exposes endpoints for querying stats, configuration, cluster health, and more without requiring application restarts.

## Table of Contents

1. [Architecture](#architecture)
2. [Standard Endpoints](#standard-endpoints)
3. [Adding Custom Handlers](#adding-custom-handlers)
4. [Query Parameters](#query-parameters)
5. [Security Considerations](#security-considerations)
6. [Performance Impact](#performance-impact)

---

## Architecture

### Components

```
┌──────────────────────────────────────────────────────┐
│                    AdminImpl                         │
│  (HTTP Server + Connection Manager Config)          │
└───────────┬──────────────────────────────────────────┘
            │
            │ Implements multiple interfaces
            │
    ┌───────┼───────┬────────────┬──────────────┐
    │       │       │            │              │
    ▼       ▼       ▼            ▼              ▼
┌────────┬────────┬──────┬───────────┬──────────────┐
│Network │Network │ Http │   Http    │  Connection  │
│Filter  │Filter  │Filter│Connection │   Manager    │
│Chain   │Chain   │Chain │  Manager  │    Config    │
│Manager │Factory │Factory│  Config   │              │
└────────┴────────┴──────┴───────────┴──────────────┘
```

### Key Files

- **Interface**: `envoy/server/admin.h`
- **Implementation**: `source/server/admin/admin.{h,cc}`
- **Handlers**: `source/server/admin/*_handler.{h,cc}`
- **Utilities**: `source/server/admin/utils.h`

### Handler Registration

**Location**: `source/server/admin/admin.cc:108-192`

```cpp
AdminImpl::AdminImpl(...) {
    handlers_ = {
        makeHandler("/", "Admin home page", MAKE_ADMIN_HANDLER(handlerAdminHome), ...), makeHandler("/stats", "print server stats", MAKE_ADMIN_HANDLER(stats_handler_.handlerStats), ...), makeHandler("/clusters", "upstream cluster status",
                   MAKE_ADMIN_HANDLER(clusters_handler_.handlerClusters), ...),
        // ... more handlers
    };
}
```

---

## Standard Endpoints

### Configuration & State

#### `/config_dump`

**Purpose**: Dump current Envoy configuration

**Handler**: `source/server/admin/config_dump_handler.h:24`

**Query Parameters**:
- `resource` - Specific resource type (e.g., `bootstrap`, `clusters`, `listeners`)
- `mask` - Field mask to filter output
- `name_regex` - Filter by name pattern
- `include_eds` - Include EDS endpoint data (default: false)

**Examples**:
```bash
# Full config dump
curl http://localhost:9901/config_dump

# Only clusters
curl http://localhost:9901/config_dump?resource=clusters

# Specific cluster by name
curl http://localhost:9901/config_dump?resource=clusters&name_regex=backend.*

# With endpoint details
curl http://localhost:9901/config_dump?resource=clusters&include_eds=true

# Mask specific fields
curl http://localhost:9901/config_dump?mask=clusters.load_assignment
```

**Output Format**: JSON (protobuf serialization)

**Response Structure**:
```json
{
  "configs": [
    {
      "@type": "type.googleapis.com/envoy.admin.v3.BootstrapConfigDump",
      "bootstrap": { ... },
      "last_updated": "2024-02-28T10:00:00.000Z"
    },
    {
      "@type": "type.googleapis.com/envoy.admin.v3.ClustersConfigDump",
      "dynamic_active_clusters": [ ... ]
    }
  ]
}
```

---

#### `/runtime`

**Purpose**: Display runtime configuration values

**Handler**: `source/server/admin/runtime_handler.h`

**Query Parameters**: None

**Example**:
```bash
curl http://localhost:9901/runtime
```

**Output**:
```json
{
  "layers": [
    {
      "name": "admin",
      "values": {
        "upstream.healthy_panic_threshold": "50",
        "upstream.zone_routing.enabled": "true"
      }
    }
  ]
}
```

---

#### `/runtime_modify`

**Purpose**: Temporarily modify runtime values (requires POST)

**HTTP Method**: POST

**Query Parameters**:
- `key1=value1&key2=value2` - Runtime keys and values to set

**Example**:
```bash
curl -X POST 'http://localhost:9901/runtime_modify?upstream.healthy_panic_threshold=30'
```

**Note**: Changes are temporary and lost on restart

---

### Stats & Metrics

#### `/stats`

**Purpose**: Display all statistics

**Handler**: `source/server/admin/stats_handler.h:79`

**Query Parameters**:
- `format` - Output format: `text` (default), `json`, `html`, `active-html`
- `filter` - Regex filter (Google RE2)
- `usedonly` - Only show used stats
- `type` - Filter by type: `Counters`, `Gauges`, `Histograms`, `TextReadouts`

**Examples**:
```bash
# All stats (text)
curl http://localhost:9901/stats

# JSON format
curl http://localhost:9901/stats?format=json

# Filter by pattern
curl 'http://localhost:9901/stats?filter=^cluster\.backend'

# Only counters
curl 'http://localhost:9901/stats?type=Counters'

# Only used stats
curl 'http://localhost:9901/stats?usedonly='

# Interactive HTML table
curl 'http://localhost:9901/stats?format=html'
```

**Output Formats**:

**Text**:
```
cluster.backend.upstream_rq_total: 12345
cluster.backend.upstream_rq_2xx: 12000
cluster.backend.upstream_rq_time: P0(1.2,…) P50(10.3,…) P99(120.5,…)
```

**JSON**:
```json
{
  "stats": [
    {
      "name": "cluster.backend.upstream_rq_total",
      "value": 12345,
      "type": "counter"
    }
  ]
}
```

---

#### `/stats/prometheus`

**Purpose**: Export stats in Prometheus format

**Handler**: `source/server/admin/stats_handler.h:31`

**Query Parameters**:
- `usedonly` - Only used stats
- `text_readouts` - Include text readouts
- `filter` - Regex filter
- `histogram_buckets` - `cumulative` or `summary`

**Example**:
```bash
curl http://localhost:9901/stats/prometheus
```

**Output**:
```prometheus
# TYPE envoy_cluster_upstream_rq_total counter
envoy_cluster_upstream_rq_total{cluster_name="backend"} 12345

# TYPE envoy_cluster_upstream_rq_time histogram
envoy_cluster_upstream_rq_time_bucket{cluster_name="backend",le="0.5"} 100
envoy_cluster_upstream_rq_time_bucket{cluster_name="backend",le="1"} 200
envoy_cluster_upstream_rq_time_bucket{cluster_name="backend",le="+Inf"} 12345
envoy_cluster_upstream_rq_time_sum{cluster_name="backend"} 54321.5
envoy_cluster_upstream_rq_time_count{cluster_name="backend"} 12345
```

---

#### `/stats/recentlookups`

**Purpose**: Debug stat name lookups (helps find typos)

**Handler**: `source/server/admin/stats_handler.h:27`

**Example**:
```bash
curl http://localhost:9901/stats/recentlookups
```

**Output**:
```
Lookup: cluster.typo_name.upstream_rq_total (Count: 1234)
Lookup: cluser.backend.upstream_rq_total (Count: 567)  # Typo: 'cluser'
```

**Control Endpoints**:
```bash
# Enable tracking
curl -X POST http://localhost:9901/stats/recentlookups/enable

# Clear history
curl -X POST http://localhost:9901/stats/recentlookups/clear

# Disable tracking
curl -X POST http://localhost:9901/stats/recentlookups/disable
```

---

#### `/reset_counters`

**Purpose**: Reset all counters to zero

**HTTP Method**: POST

**Example**:
```bash
curl -X POST http://localhost:9901/reset_counters
```

---

### Clusters & Endpoints

#### `/clusters`

**Purpose**: Display upstream cluster health and endpoints

**Handler**: `source/server/admin/clusters_handler.h`

**Query Parameters**:
- `format` - Output format: `text` (default), `json`
- `filter` - Regex filter for cluster names

**Example**:
```bash
# All clusters
curl http://localhost:9901/clusters

# Filter by name
curl 'http://localhost:9901/clusters?filter=backend.*'

# JSON format
curl 'http://localhost:9901/clusters?format=json'
```

**Output** (text format):
```
backend::observability_name::backend
backend::default_priority::max_connections::1024
backend::default_priority::max_pending_requests::1024
backend::default_priority::max_requests::1024
backend::default_priority::max_retries::3
backend::high_priority::max_connections::1024
backend::high_priority::max_pending_requests::1024
backend::high_priority::max_requests::1024
backend::high_priority::max_retries::3
backend::added_via_api::false
backend::172.16.0.1:8080::cx_active::5
backend::172.16.0.1:8080::cx_connect_fail::0
backend::172.16.0.1:8080::cx_total::100
backend::172.16.0.1:8080::rq_active::2
backend::172.16.0.1:8080::rq_error::0
backend::172.16.0.1:8080::rq_success::98
backend::172.16.0.1:8080::rq_timeout::0
backend::172.16.0.1:8080::rq_total::100
backend::172.16.0.1:8080::hostname::backend-1
backend::172.16.0.1:8080::health_flags::healthy
backend::172.16.0.1:8080::weight::1
backend::172.16.0.1:8080::region::us-west
backend::172.16.0.1:8080::zone::us-west-1a
backend::172.16.0.1:8080::sub_zone::
backend::172.16.0.1:8080::canary::false
backend::172.16.0.1:8080::priority::0
backend::172.16.0.1:8080::success_rate::-1.0
```

**Health Flags**:
- `healthy` - Healthy and available
- `failed_active_hc` - Failed active health check
- `failed_outlier_check` - Detected as outlier
- `failed_eds_health` - EDS marked unhealthy
- `degraded_active_hc` - Degraded per active health check
- `degraded_eds_health` - EDS marked degraded

---

#### `/clusters?format=json`

**JSON Output**:
```json
{
  "cluster_statuses": [
    {
      "name": "backend",
      "added_via_api": false,
      "success_rate_ejection_threshold": {
        "value": 85
      },
      "host_statuses": [
        {
          "address": {
            "socket_address": {
              "address": "172.16.0.1",
              "port_value": 8080
            }
          },
          "stats": [
            {"name": "cx_active", "value": 5},
            {"name": "rq_total", "value": 100}
          ],
          "health_status": {
            "eds_health_status": "HEALTHY"
          },
          "weight": 1,
          "locality": {
            "region": "us-west",
            "zone": "us-west-1a"
          }
        }
      ]
    }
  ]
}
```

---

### Listeners

#### `/listeners`

**Purpose**: Display active listeners and filter chains

**Handler**: `source/server/admin/listeners_handler.h`

**Query Parameters**:
- `format` - Output format: `text` (default), `json`

**Example**:
```bash
# Text format
curl http://localhost:9901/listeners

# JSON format
curl http://localhost:9901/listeners?format=json
```

**Output** (text):
```
listener_1::0.0.0.0:8080
listener_1::local_address::0.0.0.0:8080
listener_1::state::ACTIVE
listener_1::filter_chains::1
listener_1::filter_chain_0::filters::envoy.filters.network.http_connection_manager
```

---

### Server Info

#### `/server_info`

**Purpose**: Display server version and state

**Handler**: `source/server/admin/server_info_handler.h`

**Example**:
```bash
curl http://localhost:9901/server_info
```

**Output**:
```json
{
  "version": "9c42fa6f621760c09c606c882e8b85f8b675f9d8/1.29.0/Clean/RELEASE/BoringSSL",
  "state": "LIVE",
  "hot_restart_version": "11.104",
  "command_line_options": {
    "base_id": "0",
    "concurrency": 8,
    "config_path": "/etc/envoy/envoy.yaml",
    "config_yaml": "",
    "admin_address_path": "",
    "local_address_ip_version": "v4",
    "log_level": "info",
    "log_format": "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v",
    "restart_epoch": 0,
    "service_cluster": "envoy-cluster",
    "service_node": "envoy-node",
    "service_zone": "us-west-1a"
  },
  "uptime_current_epoch": "125450s",
  "uptime_all_epochs": "125450s"
}
```

**States**:
- `LIVE` - Accepting traffic
- `DRAINING` - Gracefully shutting down
- `PRE_INITIALIZING` - Starting up
- `INITIALIZING` - Loading configuration

---

#### `/ready`

**Purpose**: Readiness probe for health checks

**Example**:
```bash
curl http://localhost:9901/ready
```

**Response**:
- **200 OK** - Server is LIVE
- **503 Service Unavailable** - Server is not ready

---

#### `/certs`

**Purpose**: Display loaded TLS certificates

**Handler**: `source/server/admin/server_info_handler.h`

**Example**:
```bash
curl http://localhost:9901/certs
```

**Output**:
```json
{
  "certificates": [
    {
      "ca_cert": [
        {
          "path": "/etc/ssl/ca.crt",
          "serial_number": "1a2b3c4d",
          "subject_alt_names": [],
          "days_until_expiration": "365",
          "valid_from": "2023-02-28T00:00:00Z",
          "expiration_time": "2025-02-28T00:00:00Z"
        }
      ],
      "cert_chain": [
        {
          "path": "/etc/ssl/server.crt",
          "serial_number": "5e6f7g8h",
          "subject_alt_names": [
            {"dns": "example.com"},
            {"dns": "*.example.com"}
          ],
          "days_until_expiration": "180",
          "valid_from": "2024-08-01T00:00:00Z",
          "expiration_time": "2025-08-01T00:00:00Z"
        }
      ]
    }
  ]
}
```

---

### Memory & Performance

#### `/memory`

**Purpose**: Display memory allocation statistics

**Example**:
```bash
curl http://localhost:9901/memory
```

**Output**:
```json
{
  "allocated": "524288000",
  "heap_size": "536870912",
  "pageheap_unmapped": "8388608",
  "pageheap_free": "4194304",
  "total_thread_cache": "2097152"
}
```

---

#### `/contention`

**Purpose**: Display mutex contention statistics

**Handler**: `source/server/admin/stats_handler.h:65`

**Example**:
```bash
curl http://localhost:9901/contention
```

**Output**:
```
Mutex: ThreadLocalStoreImpl::lock_
  Contentions: 1234
  Wait time: 12.5ms
  Holder: worker_1
```

---

### Control Operations

#### `/quitquitquit`

**Purpose**: Gracefully shutdown Envoy

**HTTP Method**: POST

**Example**:
```bash
curl -X POST http://localhost:9901/quitquitquit
```

---

#### `/healthcheck/fail`

**Purpose**: Fail health checks

**HTTP Method**: POST

**Example**:
```bash
curl -X POST http://localhost:9901/healthcheck/fail
```

---

#### `/healthcheck/ok`

**Purpose**: Pass health checks

**HTTP Method**: POST

**Example**:
```bash
curl -X POST http://localhost:9901/healthcheck/ok
```

---

#### `/drain_listeners`

**Purpose**: Start draining listeners

**HTTP Method**: POST

**Query Parameters**:
- `inboundonly` - Only drain inbound listeners
- `graceful` - Wait for connections to close

**Example**:
```bash
curl -X POST 'http://localhost:9901/drain_listeners?graceful'
```

---

#### `/logging`

**Purpose**: Query or modify logging levels

**HTTP Method**: POST (to modify)

**Query Parameters** (POST):
- `<logger>=<level>` - Set logger to level

**Example**:
```bash
# Query current levels
curl http://localhost:9901/logging

# Set level
curl -X POST 'http://localhost:9901/logging?http=debug'

# Set multiple
curl -X POST 'http://localhost:9901/logging?http=debug&router=trace'
```

**Levels**: `trace`, `debug`, `info`, `warning`, `error`, `critical`, `off`

---

## Adding Custom Handlers

### Static Handler

**Location**: `source/server/admin/admin.cc`

```cpp
// In AdminImpl constructor
handlers_.push_back(
    makeHandler("/my_custom_endpoint", "My custom endpoint description", MAKE_ADMIN_HANDLER(handlerMyCustomEndpoint), /*removable=*/false, /*mutates_state=*/false)
);

// Implement handler
Http::Code AdminImpl::handlerMyCustomEndpoint(
    Http::ResponseHeaderMap& response_headers, Buffer::Instance& response, AdminStream& admin_stream) {

    // Query parameters
    const auto params = admin_stream.queryParams();
    const auto filter = params.getFirstValue("filter");

    // Generate response
    response.add("Custom response data\n");

    return Http::Code::OK;
}
```

### Dynamic Handler

```cpp
// Add handler dynamically
server.admin().addHandler(
    "/dynamic_endpoint", "Dynamic endpoint", [](Http::ResponseHeaderMap&, Buffer::Instance& response, Server::AdminStream&) -> Http::Code {
        response.add("Dynamic handler response\n");
        return Http::Code::OK;
    }, /*removable=*/true, /*mutates_state=*/false
);

// Remove handler later
server.admin().removeHandler("/dynamic_endpoint");
```

### Streaming Handler

For large responses that should be streamed:

```cpp
class MyStreamingHandler {
public:
    Admin::RequestPtr makeRequest(AdminStream& admin_stream) {
        return std::make_unique<MyRequest>(admin_stream);
    }

private:
    class MyRequest : public Admin::Request {
    public:
        MyRequest(AdminStream& admin_stream)
            : Request(admin_stream) {
            // Don't end stream on complete
            admin_stream.setEndStreamOnComplete(false);
        }

        Http::Code start(Http::ResponseHeaderMap&) override {
            // Start streaming
            startStreaming();
            return Http::Code::OK;
        }

        void startStreaming() {
            // Send chunks
            Buffer::OwnedImpl chunk;
            chunk.add("Chunk 1\n");
            getDecoderFilterCallbacks().encodeData(chunk, false);

            // More chunks...
            chunk.add("Chunk 2\n");
            getDecoderFilterCallbacks().encodeData(chunk, true);  // end_stream
        }
    };
};
```

---

## Query Parameters

### Common Parameters

Most endpoints support these:

- `format` - Output format: `text`, `json`, `html`
- `filter` - Regex filter (Google RE2 syntax)

### Regular Expression Syntax

Uses Google RE2 library:

```bash
# Match prefix
curl 'http://localhost:9901/stats?filter=^cluster\.backend'

# Match suffix
curl 'http://localhost:9901/stats?filter=upstream_rq_total$'

# Match pattern
curl 'http://localhost:9901/stats?filter=cluster\.(backend|frontend)\..*_rq_'

# Case insensitive (use (?i))
curl 'http://localhost:9901/stats?filter=(?i)BACKEND'
```

---

## Security Considerations

### Network Access

Admin interface should be restricted:

```yaml
admin:
  address:
    socket_address:
      # Bind to localhost only
      address: 127.0.0.1
      port_value: 9901
```

### Firewall Rules

```bash
# Allow only from specific IP
iptables -A INPUT -p tcp --dport 9901 -s 10.0.1.0/24 -j ACCEPT
iptables -A INPUT -p tcp --dport 9901 -j DROP
```

### Authentication

Admin interface does NOT provide built-in authentication. Options:

1. **Network isolation** (localhost/internal network only)
2. **Reverse proxy** with authentication
3. **mTLS** using transport sockets
4. **Path allowlisting**:

```cpp
admin.addAllowlistedPath(
    std::make_unique<Matchers::ExactMatcher>("/stats")
);
```

### Mutation Operations

Operations that mutate state require POST:
- `/quitquitquit`
- `/reset_counters`
- `/logging` (when modifying)
- `/healthcheck/fail`
- `/drain_listeners`

This prevents accidental mutations from GET requests.

---

## Performance Impact

### Query Cost

| Endpoint | CPU Cost | Memory | Notes |
|----------|----------|--------|-------|
| `/stats` | O(N) stats | Low | Merges TLS stats |
| `/config_dump` | O(1) | Medium | Serializes config |
| `/clusters` | O(N) hosts | Low | Iterates endpoints |
| `/listeners` | O(N) listeners | Low | Small dataset |
| `/memory` | O(1) | Low | tcmalloc stats |

### Best Practices

1. **Use filters** to reduce response size
2. **Cache responses** if querying frequently
3. **Query during low traffic** for expensive operations
4. **Use `/ready`** for health checks (lightweight)
5. **Avoid querying `/config_dump`** too frequently

### Monitoring Admin Usage

Track admin requests:

```bash
curl http://localhost:9901/stats | grep admin
```

Look for:
- `http.admin.downstream_rq_total`
- `http.admin.downstream_rq_time`

---

## Troubleshooting

### Admin Not Responding

Check if admin listener is up:

```bash
netstat -an | grep 9901
```

Check logs for admin startup errors:

```bash
grep admin /var/log/envoy.log
```

### Large Response Times

```bash
# Add filter to reduce response size
curl 'http://localhost:9901/stats?filter=^cluster\.my_cluster'

# Use Prometheus format (more efficient)
curl http://localhost:9901/stats/prometheus
```

### Permission Denied

Admin requires sufficient file descriptor limits:

```bash
# Check limits
ulimit -n

# Increase if needed
ulimit -n 65536
```

---

## See Also

- [OVERVIEW.md](./OVERVIEW.md) - Observability overview
- [stats_subsystem.md](./stats_subsystem.md) - Stats details
- `envoy/server/admin.h` - Admin interface
- `source/server/admin/README.md` - Admin implementation notes
