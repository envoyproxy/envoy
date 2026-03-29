# HTTP Status Codes Implementation

**File**: `/source/common/http/codes.h`
**Interface**: `/envoy/http/codes.h`
**Purpose**: Efficient HTTP status code tracking and statistics

## Overview

The codes implementation provides:
- **Lazy status code symbol allocation** - Only creates symbols for codes actually used
- **Thread-safe symbol creation** - Uses atomics to avoid mutex contention
- **Efficient stat tracking** - Pre-allocated stat names for common patterns
- **Canary support** - Separate stats for canary deployments
- **Zone awareness** - Track stats by source/destination zones

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│              HTTP Response Event                          │
│                       ↓                                    │
│  ┌────────────────────────────────────────────────────┐  │
│  │        chargeResponseStat()                        │  │
│  │   (Called by connection manager on response)       │  │
│  └────────────────────────────────────────────────────┘  │
│                       ↓                                    │
│  ┌────────────────────────────────────────────────────┐  │
│  │     Determine Code Groups & Categories             │  │
│  │  - 2xx, 4xx, 5xx (group)                          │  │
│  │  - Exact code (200, 404, 500)                     │  │
│  │  - Canary vs normal                                │  │
│  │  - Internal vs external                            │  │
│  │  - Zone routing                                    │  │
│  └────────────────────────────────────────────────────┘  │
│                       ↓                                    │
│  ┌────────────────────────────────────────────────────┐  │
│  │        Increment Counters                          │  │
│  │  - cluster.upstream_rq_2xx                        │  │
│  │  - cluster.upstream_rq_200                        │  │
│  │  - vhost.vhost_name.vcluster.vcluster_name.2xx    │  │
│  │  - cluster.canary.upstream_rq_2xx                 │  │
│  │  - cluster.zone.from_zone.to_zone.upstream_rq_2xx │  │
│  └────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

## Key Classes

### CodeStatsImpl

Implementation of the `CodeStats` interface with efficient stat tracking.

```cpp
class CodeStatsImpl : public CodeStats {
public:
    explicit CodeStatsImpl(Stats::SymbolTable& symbol_table);

    // Charge basic response stat (minimal tracking)
    void chargeBasicResponseStat(
        Stats::Scope& scope,
        Stats::StatName prefix,
        Code response_code,
        bool exclude_http_code_stats
    ) const override;

    // Charge full response stat (includes canary, zones, vhost, etc.)
    void chargeResponseStat(
        const ResponseStatInfo& info,
        bool exclude_http_code_stats
    ) const override;

    // Charge response timing histogram
    void chargeResponseTiming(
        const ResponseTimingInfo& info
    ) const override;

private:
    // Lazy symbol allocation for status codes
    mutable Thread::AtomicPtrArray<const uint8_t, NumHttpCodes,
                                   Thread::AtomicPtrAllocMode::DoNotDelete> rc_stat_names_;

    Stats::StatNamePool stat_name_pool_;
    Stats::SymbolTable& symbol_table_;

    // Pre-allocated common stat names
    const Stats::StatName canary_;
    const Stats::StatName external_;
    const Stats::StatName internal_;
    const Stats::StatName upstream_;
    const Stats::StatName upstream_rq_1xx_;
    const Stats::StatName upstream_rq_2xx_;
    const Stats::StatName upstream_rq_3xx_;
    const Stats::StatName upstream_rq_4xx_;
    const Stats::StatName upstream_rq_5xx_;
    const Stats::StatName upstream_rq_unknown_;
    const Stats::StatName upstream_rq_completed_;
    const Stats::StatName upstream_rq_time_;
    const Stats::StatName vcluster_;
    const Stats::StatName vhost_;
    const Stats::StatName route_;
    const Stats::StatName zone_;
};
```

### ResponseStatInfo

Context information for charging response statistics:

```cpp
struct CodeStats::ResponseStatInfo {
    // Stat scopes for different aggregation levels
    Stats::Scope& global_scope_;        // Global stats
    Stats::Scope& cluster_scope_;       // Per-cluster stats

    // Basic response info
    Stats::StatName prefix_;            // "upstream_rq" typically
    uint64_t response_status_code_;     // 200, 404, 500, etc.
    bool internal_request_;             // Internal vs external

    // Routing context
    Stats::StatName request_vhost_name_;     // Virtual host name
    Stats::StatName request_vcluster_name_;  // Virtual cluster name
    Stats::StatName request_route_name_;     // Route name

    // Zone awareness
    Stats::StatName from_zone_;         // Source zone
    Stats::StatName to_zone_;           // Destination zone

    // Canary deployment
    bool upstream_canary_;              // Is upstream a canary instance
};
```

### ResponseTimingInfo

Context for recording response timing histograms:

```cpp
struct CodeStats::ResponseTimingInfo {
    Stats::Scope& global_scope_;
    Stats::Scope& cluster_scope_;
    Stats::StatName prefix_;
    std::chrono::milliseconds response_time_;
    bool upstream_canary_;
    bool internal_request_;
    Stats::StatName request_vhost_name_;
    Stats::StatName request_vcluster_name_;
    Stats::StatName request_route_name_;
    Stats::StatName from_zone_;
    Stats::StatName to_zone_;
};
```

### CodeUtility

Helper functions for status code operations:

```cpp
class CodeUtility {
public:
    // Convert code to string description
    static const char* toString(Code code);

    // Check code category
    static bool is1xx(uint64_t code) { return code >= 100 && code < 200; }
    static bool is2xx(uint64_t code) { return code >= 200 && code < 300; }
    static bool is3xx(uint64_t code) { return code >= 300 && code < 400; }
    static bool is4xx(uint64_t code) { return code >= 400 && code < 500; }
    static bool is5xx(uint64_t code) { return code >= 500 && code < 600; }

    // Gateway errors (502, 503, 504)
    static bool isGatewayError(uint64_t code) {
        return code >= 502 && code < 505;
    }

    // Get group string ("2xx", "4xx", etc.)
    static std::string groupStringForResponseCode(Code response_code);
};
```

## Statistics Generated

### Cluster-Level Stats

For each response, stats are incremented in the cluster scope:

```
cluster.<cluster_name>.upstream_rq_<group>       # e.g., upstream_rq_2xx
cluster.<cluster_name>.upstream_rq_<code>        # e.g., upstream_rq_200
cluster.<cluster_name>.upstream_rq_completed
```

### Canary Stats

If the upstream is a canary instance:

```
cluster.<cluster_name>.canary.upstream_rq_<group>
cluster.<cluster_name>.canary.upstream_rq_<code>
cluster.<cluster_name>.canary.upstream_rq_completed
```

### Internal vs External

```
cluster.<cluster_name>.internal.upstream_rq_<group>    # Internal requests
cluster.<cluster_name>.external.upstream_rq_<group>    # External requests
```

### Virtual Host Stats

```
vhost.<vhost_name>.upstream_rq_<group>
vhost.<vhost_name>.upstream_rq_<code>
```

### Virtual Cluster Stats

```
vhost.<vhost_name>.vcluster.<vcluster_name>.upstream_rq_<group>
vhost.<vhost_name>.vcluster.<vcluster_name>.upstream_rq_<code>
```

### Route Stats

```
vhost.<vhost_name>.route.<route_name>.upstream_rq_<group>
vhost.<vhost_name>.route.<route_name>.upstream_rq_<code>
```

### Zone Stats

If zone routing is configured:

```
cluster.<cluster_name>.zone.<from_zone>.<to_zone>.upstream_rq_<group>
cluster.<cluster_name>.zone.<from_zone>.<to_zone>.upstream_rq_<code>
```

### Timing Histograms

Response time tracking:

```
cluster.<cluster_name>.upstream_rq_time                    # All requests
cluster.<cluster_name>.canary.upstream_rq_time            # Canary requests
cluster.<cluster_name>.internal.upstream_rq_time          # Internal requests
vhost.<vhost_name>.upstream_rq_time
vhost.<vhost_name>.vcluster.<vcluster_name>.upstream_rq_time
```

## Implementation Details

### Lazy Symbol Allocation

Status code symbols are allocated lazily to optimize symbol table usage:

```cpp
// Status codes 100-599 (500 total codes)
static constexpr uint32_t NumHttpCodes = 500;
static constexpr uint32_t HttpCodeOffset = 100;  // Code 100 is at index 0

// Atomic pointer array for thread-safe lazy allocation
mutable Thread::AtomicPtrArray<const uint8_t, NumHttpCodes,
                               Thread::AtomicPtrAllocMode::DoNotDelete> rc_stat_names_;
```

**Why Lazy Allocation?**
- Symbol tables encode the first 128 symbols with single bytes
- Pre-allocating all 500 codes would waste valuable single-byte encodings
- Most services only use a handful of status codes (200, 404, 500, 503)
- Lazy allocation reserves single-byte codes for actually-used codes

### Thread Safety

The implementation uses atomic pointers to avoid mutex contention:

```cpp
// Fast path: Check if symbol already allocated (atomic read, no barrier)
if (rc_stat_names_[code_index] != nullptr) {
    return rc_stat_names_[code_index];
}

// Slow path: Allocate new symbol (mutex protected, rare)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (rc_stat_names_[code_index] == nullptr) {
        auto stat_name = allocateStatName(code);
        rc_stat_names_[code_index] = stat_name;
    }
}
return rc_stat_names_[code_index];
```

### Stat Name Composition

Stats are composed from pre-allocated components:

```cpp
void incCounter(Stats::Scope& scope, Stats::StatName a, Stats::StatName b) const {
    // Efficient: No string concatenation, just symbol table composition
    Stats::StatNameVec names = {a, b};
    scope.counterFromStatNameVec(names).inc();
}
```

**Example**:
```cpp
// Instead of: "cluster." + cluster_name + ".upstream_rq_2xx"
// Uses: StatNameVec{upstream_, rq_2xx_}
incCounter(cluster_scope_, upstream_, upstream_rq_2xx_);
```

## Usage Examples

### Example 1: Basic Response Stat Charging

```cpp
void ConnectionManagerImpl::chargeStats(const ResponseHeaderMap& headers) {
    uint64_t response_code = Utility::getResponseStatus(headers);

    // Simple case: Just charge the basic stats
    code_stats_.chargeBasicResponseStat(
        cluster_scope_,
        upstream_rq_,  // "upstream_rq" prefix
        static_cast<Code>(response_code),
        false  // Don't exclude HTTP code stats
    );
}
```

This increments:
- `cluster.my_cluster.upstream_rq_2xx` (if response is 200)
- `cluster.my_cluster.upstream_rq_200`

### Example 2: Full Response Stat Charging

```cpp
void RouterFilter::chargeResponseStats() {
    CodeStats::ResponseStatInfo info{
        .global_scope_ = config_->scope(),
        .cluster_scope_ = cluster_->statsScope(),
        .prefix_ = upstream_rq_stat_name_,
        .response_status_code_ = response_code_,
        .internal_request_ = is_internal_request_,
        .request_vhost_name_ = route_entry_->virtualHost().statName(),
        .request_vcluster_name_ = route_entry_->virtualCluster().statName(),
        .request_route_name_ = route_entry_->routeName(),
        .from_zone_ = downstream_zone_,
        .to_zone_ = upstream_zone_,
        .upstream_canary_ = upstream_host_->canary()
    };

    config_->http_context_.codeStats().chargeResponseStat(info, false);
}
```

This increments many stats:
- Cluster stats: `cluster.my_cluster.upstream_rq_2xx`
- Canary stats: `cluster.my_cluster.canary.upstream_rq_2xx` (if canary)
- VHost stats: `vhost.my_vhost.upstream_rq_2xx`
- VCluster stats: `vhost.my_vhost.vcluster.api.upstream_rq_2xx`
- Zone stats: `cluster.my_cluster.zone.us-east-1.us-west-2.upstream_rq_2xx`
- Internal/External: `cluster.my_cluster.internal.upstream_rq_2xx`

### Example 3: Response Timing

```cpp
void recordResponseTime() {
    auto response_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        time_source_.monotonicTime() - request_start_time_
    );

    CodeStats::ResponseTimingInfo timing_info{
        .global_scope_ = config_->scope(),
        .cluster_scope_ = cluster_->statsScope(),
        .prefix_ = upstream_rq_stat_name_,
        .response_time_ = response_time,
        .upstream_canary_ = upstream_host_->canary(),
        .internal_request_ = is_internal_request_,
        .request_vhost_name_ = vhost_stat_name_,
        .request_vcluster_name_ = vcluster_stat_name_,
        .request_route_name_ = route_stat_name_,
        .from_zone_ = downstream_zone_,
        .to_zone_ = upstream_zone_
    };

    config_->http_context_.codeStats().chargeResponseTiming(timing_info);
}
```

This records histograms:
- `cluster.my_cluster.upstream_rq_time` (value: 25ms)
- `vhost.my_vhost.upstream_rq_time` (value: 25ms)
- `cluster.my_cluster.canary.upstream_rq_time` (if canary)

### Example 4: Code Utility Functions

```cpp
void processResponse(uint64_t status_code) {
    // Check response category
    if (CodeUtility::is2xx(status_code)) {
        // Success
    } else if (CodeUtility::is4xx(status_code)) {
        // Client error
    } else if (CodeUtility::is5xx(status_code)) {
        // Server error
    }

    // Check if gateway error (502, 503, 504)
    if (CodeUtility::isGatewayError(status_code)) {
        // Retry logic
        maybeRetry();
    }

    // Convert to string for logging
    const char* code_str = CodeUtility::toString(static_cast<Code>(status_code));
    ENVOY_LOG(info, "Response: {}", code_str);  // "Response: OK" or "Response: Not Found"

    // Get group string
    std::string group = CodeUtility::groupStringForResponseCode(
        static_cast<Code>(status_code)
    );
    // Returns "2xx", "4xx", "5xx", etc.
}
```

## Performance Considerations

### 1. Hot Path Optimization

The most common path (charging stats for an already-seen code) is optimized:

```cpp
// Fast: Atomic read with no lock
auto* stat_name = rc_stat_names_[code_index];
if (stat_name != nullptr) {
    // Use pre-allocated symbol
}
```

### 2. Symbol Table Efficiency

Pre-allocating common stat names reduces runtime string operations:

```cpp
// Constructor pre-allocates common names
CodeStatsImpl::CodeStatsImpl(Stats::SymbolTable& symbol_table)
    : symbol_table_(symbol_table),
      canary_(stat_name_pool_.add("canary")),
      upstream_(stat_name_pool_.add("upstream")),
      upstream_rq_2xx_(stat_name_pool_.add("upstream_rq_2xx")),
      // ... etc
{}
```

### 3. Stat Name Vector Composition

Stats are composed without string concatenation:

```cpp
// Efficient: No allocations
Stats::StatNameVec names = {vhost_, vhost_name, vcluster_, vcluster_name, upstream_rq_2xx_};
scope.counterFromStatNameVec(names).inc();

// vs. Inefficient (what this avoids):
// std::string stat_name = "vhost." + vhost_name + ".vcluster." + vcluster_name + ".upstream_rq_2xx";
```

### 4. Conditional Stat Charging

The `exclude_http_code_stats` flag allows bypassing expensive stat operations:

```cpp
// For internal health checks, skip detailed stats
code_stats_.chargeBasicResponseStat(
    scope, prefix, code,
    true  // exclude_http_code_stats - skip group/code-specific stats
);
```

## Configuration

### Excluding HTTP Code Stats

In the HTTP connection manager config:

```yaml
http_filters:
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
      # Suppress HTTP code stats for specific routes/clusters
      suppress_envoy_headers: true
```

### Zone-Aware Routing

Enable zone stats by configuring locality:

```yaml
clusters:
  - name: my_cluster
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    # Zone awareness config
    common_lb_config:
      zone_aware_lb_config:
        routing_enabled:
          default: 100
        min_cluster_size: 6
```

This enables stats like:
- `cluster.my_cluster.zone.us-east-1.us-east-1.upstream_rq_2xx`
- `cluster.my_cluster.zone.us-east-1.us-west-2.upstream_rq_2xx`

## Best Practices

### 1. Use Appropriate Granularity

**For high-volume services**:
- Use `chargeBasicResponseStat()` for internal health checks
- Use `chargeResponseStat()` for user-facing requests

**For low-volume services**:
- Always use `chargeResponseStat()` for full visibility

### 2. Monitor Key Stats

Focus on these critical stats:

```
# Overall health
cluster.<name>.upstream_rq_2xx
cluster.<name>.upstream_rq_5xx

# Error rates
cluster.<name>.upstream_rq_503  # Service unavailable
cluster.<name>.upstream_rq_504  # Gateway timeout

# Canary health
cluster.<name>.canary.upstream_rq_5xx

# Response time
cluster.<name>.upstream_rq_time (P50, P95, P99)
```

### 3. Alert on Anomalies

Set up alerts for:
- **5xx rate > 1%**: `cluster.<name>.upstream_rq_5xx / cluster.<name>.upstream_rq_completed > 0.01`
- **Gateway errors**: `cluster.<name>.upstream_rq_503 + cluster.<name>.upstream_rq_504 > threshold`
- **Canary divergence**: `cluster.<name>.canary.upstream_rq_5xx / cluster.<name>.upstream_rq_5xx > 2.0`

### 4. Zone Awareness

For multi-zone deployments:
- Monitor cross-zone traffic
- Alert on unexpected cross-zone routing
- Track zone-specific error rates

```
# Cross-zone traffic
cluster.<name>.zone.us-east-1.us-west-2.upstream_rq_completed

# Same-zone error rate
cluster.<name>.zone.us-east-1.us-east-1.upstream_rq_5xx
```

## Summary

The HTTP codes implementation provides:

- **Efficient stat tracking** with lazy symbol allocation
- **Thread-safe operations** using atomics
- **Rich stat dimensions**: cluster, vhost, vcluster, route, zone, canary
- **Performance optimized** for hot paths
- **Flexible granularity** (basic vs full stats)

**Key Takeaway**: The design optimizes for the common case (charging stats for frequently-seen codes) while supporting comprehensive stat dimensions for debugging and monitoring.
