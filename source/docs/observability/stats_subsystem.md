# Stats Subsystem - Detailed Documentation

## Overview

The stats subsystem provides low-overhead metrics collection for monitoring Envoy's behavior. It uses thread-local storage and lock-free operations to minimize performance impact while collecting thousands of metrics per second.

## Table of Contents

1. [Architecture](#architecture)
2. [Stat Types](#stat-types)
3. [Thread-Local Design](#thread-local-design)
4. [Symbol Table](#symbol-table)
5. [Stats Sinks](#stats-sinks)
6. [Creating Stats](#creating-stats)
7. [Performance Considerations](#performance-considerations)

---

## Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────┐
│                    ThreadLocalStoreImpl                  │
│                    (Main Thread Store)                   │
└────────────┬────────────────────────────────────────────┘
             │
             │ Owns TLS slot
             │
    ┌────────┴────────┬───────────┬───────────┐
    │                 │           │           │
    ▼                 ▼           ▼           ▼
┌─────────┐      ┌─────────┐ ┌─────────┐ ┌─────────┐
│ TLS Slot│      │ TLS Slot│ │ TLS Slot│ │ TLS Slot│
│ Thread 1│      │ Thread 2│ │ Thread 3│ │ Thread 4│
└─────────┘      └─────────┘ └─────────┘ └─────────┘
    │                 │           │           │
    │                 │           │           │
Lock-free          Lock-free  Lock-free  Lock-free
recording          recording  recording  recording
    │                 │           │           │
    └────────┬────────┴───────────┴───────────┘
             │
             │ Periodic merge (with locks)
             │
             ▼
    ┌──────────────────┐
    │  Parent Stats    │
    │  (Aggregated)    │
    └──────────────────┘
             │
             ├──→ Admin /stats endpoint
             ├──→ Prometheus exporter
             └──→ Stats sinks (statsd, etc.)
```

### Key Files

- **Interfaces**: `envoy/stats/stats.h`, `envoy/stats/histogram.h`
- **Implementation**: `source/common/stats/thread_local_store.{h,cc}`
- **Symbol Table**: `source/common/stats/symbol_table.{h,cc}`
- **Allocator**: `source/common/stats/allocator_impl.{h,cc}`

---

## Stat Types

### 1. Counter (`envoy/stats/stats.h:126-135`)

**Purpose**: Monotonically increasing values

**Interface**:
```cpp
class Counter : public Metric {
public:
    virtual void inc() PURE;                  // Increment by 1
    virtual void add(uint64_t amount) PURE;   // Increment by amount
    virtual uint64_t value() const PURE;      // Current value
    virtual uint64_t latch() PURE;            // Get & reset period counter
    virtual void reset() PURE;                // Reset to zero
};
```

**Common Uses**:
- Request counts: `cluster.service.upstream_rq_total`
- Error counts: `cluster.service.upstream_rq_5xx`
- Connection counts: `cluster.service.upstream_cx_total`

**Example**:
```cpp
stats.upstream_rq_total_.inc();
stats.upstream_rq_bytes_.add(response_size);
```

### 2. Gauge (`envoy/stats/stats.h:142-194`)

**Purpose**: Values that can increase or decrease

**Interface**:
```cpp
class Gauge : public Metric {
public:
    virtual void inc() PURE;                   // Increment by 1
    virtual void dec() PURE;                   // Decrement by 1
    virtual void add(uint64_t amount) PURE;    // Add amount
    virtual void sub(uint64_t amount) PURE;    // Subtract amount
    virtual void set(uint64_t value) PURE;     // Set to value
    virtual uint64_t value() const PURE;       // Current value

    // Optimized for simultaneous add/sub
    inline void adjust(uint64_t add_amount, uint64_t sub_amount);
};
```

**Import Modes** (for hot restart):
- `Accumulate` - Transfer gauge state on hot-restart
- `NeverImport` - Start at 0 on hot-restart
- `HiddenAccumulate` - Transfer but hide from admin/sinks

**Common Uses**:
- Active connections: `cluster.service.upstream_cx_active`
- Memory usage: `server.memory_allocated`
- Queue sizes: `cluster.service.upstream_rq_pending_active`

**Example**:
```cpp
stats.upstream_cx_active_.inc();  // New connection
// ... connection processing ...
stats.upstream_cx_active_.dec();  // Connection closed
```

### 3. Histogram (`envoy/stats/histogram.h:109-147`)

**Purpose**: Record value distributions with percentiles and buckets

**Interface**:
```cpp
class Histogram : public Metric {
public:
    enum class Unit {
        Unspecified, Bytes, Microseconds, Milliseconds, Percent,  // Fixed-point: value * PercentScale
    };

    virtual Unit unit() const PURE;
    virtual void recordValue(uint64_t value) PURE;
};

class ParentHistogram : public Histogram {
public:
    virtual void merge() PURE;  // Merge TLS histograms
    virtual const HistogramStatistics& intervalStatistics() const PURE;
    virtual const HistogramStatistics& cumulativeStatistics() const PURE;
    virtual std::string quantileSummary() const PURE;
    virtual std::string bucketSummary() const PURE;
};
```

**Statistics Provided**:
- Percentiles: P0, P25, P50, P75, P90, P95, P99, P99.9, P100
- Buckets: Configurable upper bounds with counts
- Sample count and sum

**Common Uses**:
- Request latency: `cluster.service.upstream_rq_time`
- Response sizes: `cluster.service.upstream_rq_bytes`
- Processing time: `http.ingress.downstream_rq_time`

**Example**:
```cpp
// Record latency in milliseconds
uint64_t latency_ms = duration.count();
stats.upstream_rq_time_.recordValue(latency_ms);
```

---

## Thread-Local Design

### The Problem

Recording stats millions of times per second with atomic operations would create severe contention. Each atomic increment involves cache line invalidation across CPU cores.

### The Solution: Thread-Local Storage

Each worker thread maintains its own copy of stats. Recording is lock-free and uses thread-local storage.

### Implementation: `ThreadLocalStoreImpl`

**Location**: `source/common/stats/thread_local_store.h:150`

```cpp
class ThreadLocalStoreImpl : public StoreRoot {
    // Main thread holds parent stats
    absl::flat_hash_map<StatName, ParentHistogramSharedPtr> histograms_;
    absl::flat_hash_map<StatName, CounterSharedPtr> counters_;
    absl::flat_hash_map<StatName, GaugeSharedPtr> gauges_;

    // Thread-local slot
    ThreadLocal::SlotPtr tls_;
};
```

### Recording Flow

**Step 1: Worker Thread Records (Lock-Free)**
```
Worker Thread 1                      Thread-Local Storage
    ↓                                        ↓
counter->inc()  ────────────────→  TLS Counter (atomic)
                 No locks!          Increment local value
```

**Step 2: Periodic Merge (Main Thread)**
```
Main Thread                     TLS from all workers
    ↓                                   ↓
Merge timer fires
    ↓
for each TLS:                   Thread 1: +100
    lock TLS                    Thread 2: +150
    read & reset value          Thread 3: +200
    unlock                      Thread 4: +50
    ↓
Parent Counter += 500
```

### Histogram Double-Buffering

**Problem**: Histograms can't be merged atomically (complex data structure)

**Solution**: Each TLS histogram has two buffers

```cpp
class ThreadLocalHistogramImpl {
    histogram_t* histograms_[2];  // Double buffer
    uint64_t current_active_;     // 0 or 1

    void beginMerge() {
        // Swap active buffer
        current_active_ = 1 - current_active_;
    }

    void recordValue(uint64_t value) override {
        // Record to currently active buffer (lock-free)
        hist_insert(histograms_[current_active_], value, 1);
    }
};
```

**Merge Flow**:
```
1. Main thread calls beginMerge() on all TLS histograms
   → Swaps active buffer (workers now record to other buffer)

2. Main thread merges inactive buffers (no contention)
   → Workers continue recording to active buffer

3. Next merge cycle: repeat with buffers swapped
```

---

## Symbol Table

### The Problem

Stat names like `cluster.my_service_v2.upstream_rq_total` consume significant memory when stored thousands of times (once per worker, plus parent).

### The Solution: Symbol Table Encoding

**Location**: `source/common/stats/symbol_table.h`

```cpp
class SymbolTable {
    // Encode strings into symbols
    Symbol encode(absl::string_view name);

    // Decode symbols back to strings
    std::string decode(const SymbolVec& symbols) const;
};
```

**Encoding Example**:
```
String: "cluster.my_service.upstream_rq_total"
   ↓
Symbols: [17, 42, 103]  (3 integers instead of 37 characters)
   ↓
Storage: ~12 bytes instead of ~37 bytes
```

**Memory Savings**:
- 10,000 stats × 50 chars × 10 workers = 5 MB
- 10,000 stats × 3 symbols × 4 bytes × 10 workers = 1.2 MB
- **Savings: ~75%**

### StatName: Encoded Reference

```cpp
class StatName {
    // Compact reference to encoded symbol sequence
    // Only stores pointer + size, not the actual string
};
```

**Usage**:
```cpp
// Create stat with name encoded as symbols
StatNameManagedStorage name("cluster.service.upstream_rq_total", symbol_table);
Counter& counter = scope.counterFromStatName(name.statName());
```

---

## Stats Sinks

### Purpose

Export stats to external monitoring systems.

### Built-in Sinks

1. **StatsD** (`source/common/stats/statsd.cc`)
   - UDP-based protocol
   - Sends individual metric updates
   - Low overhead

2. **DogStatsD** (StatsD extension)
   - Adds tags/dimensions
   - Compatible with DataDog

3. **Prometheus** (via admin endpoint)
   - HTTP pull model via `/stats/prometheus`
   - Converts Envoy stats to Prometheus format

4. **gRPC Stats Sink**
   - Push metrics via gRPC
   - Structured protobuf format

### Sink Configuration

```yaml
stats_sinks:
  - name: envoy.stat_sinks.statsd
    typed_config:
      "@type": type.googleapis.com/envoy.config.metrics.v3.StatsdSink
      address:
        socket_address:
          address: 127.0.0.1
          port_value: 8125
      prefix: envoy
```

### Flush Interval

Stats are flushed periodically (default: 5 seconds):

```yaml
stats_flush_interval: 5s
```

---

## Creating Stats

### Method 1: DEFINE_STATS Macro

**Location**: `envoy/stats/stats_macros.h`

```cpp
// In header file
#define MY_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)  \
  COUNTER(requests)                                  \
  COUNTER(errors)                                    \
  GAUGE(active_requests)                             \
  HISTOGRAM(request_duration, Milliseconds)

struct MyFilterStats {
    MY_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};
```

**Usage**:
```cpp
// In implementation
MyFilterStats stats = MY_FILTER_STATS(
    POOL_COUNTER_PREFIX(scope, "my_filter."), POOL_GAUGE_PREFIX(scope, "my_filter."), POOL_HISTOGRAM_PREFIX(scope, "my_filter.")
);

stats.requests_.inc();
stats.active_requests_.inc();
stats.request_duration_.recordValue(duration_ms);
```

### Method 2: Dynamic Creation

```cpp
// Create counter dynamically
Stats::Counter& counter = scope.counterFromString("my.dynamic.counter");
counter.inc();

// Create gauge
Stats::Gauge& gauge = scope.gaugeFromString(
    "my.dynamic.gauge", Stats::Gauge::ImportMode::Accumulate
);
gauge.set(42);

// Create histogram
Stats::Histogram& histogram = scope.histogramFromString(
    "my.dynamic.histogram", Stats::Histogram::Unit::Milliseconds
);
histogram.recordValue(100);
```

### Method 3: StatName-based (Most Efficient)

```cpp
// Pre-encode stat name
StatNameManagedStorage stat_name("my.stat.name", symbol_table);

// Create stat with encoded name
Stats::Counter& counter = scope.counterFromStatName(stat_name.statName());
counter.inc();
```

---

## Performance Considerations

### Recording Performance

**Lock-free path (thread-local)**:
- Counter increment: ~2-5 CPU cycles
- Gauge set: ~2-5 CPU cycles
- Histogram record: ~50-100 CPU cycles (histogram insertion)

**Locked path (rare)**:
- Stat creation: ~1000 CPU cycles (requires allocation and registration)

### Memory Usage

**Per stat overhead** (with 10 worker threads):
- Counter: ~60 bytes (parent) + ~20 bytes × 10 (TLS) = ~260 bytes
- Gauge: ~60 bytes (parent) + ~20 bytes × 10 (TLS) = ~260 bytes
- Histogram: ~200 bytes (parent) + ~100 bytes × 10 (TLS) = ~1200 bytes

**With 10,000 stats**:
- 8,000 counters × 260 bytes = 2 MB
- 1,500 gauges × 260 bytes = 390 KB
- 500 histograms × 1200 bytes = 600 KB
- **Total: ~3 MB**

### Optimization Tips

1. **Use StatName encoding** for frequently created stats
2. **Reuse stat references** instead of looking up repeatedly
3. **Avoid creating stats in hot path** (create once, reuse)
4. **Use macros for compile-time stat definitions**
5. **Configure histogram buckets** to match your data distribution

### Deferred Stats

For rarely-used stat blocks, defer creation:

```cpp
// Define deferred stats
Stats::DeferredCreationCompatibleStats<MyStats> deferred_stats_;

// Stats created only when first accessed
deferred_stats_->counter_.inc();  // Creates on first use

// Check if created
if (deferred_stats_.isPresent()) {
    // Stats have been instantiated
}
```

---

## Stat Naming Conventions

### Standard Prefixes

- `cluster.<name>.` - Cluster stats
- `listener.<address>.` - Listener stats
- `http.<stat_prefix>.` - HTTP connection manager
- `server.` - Server-wide stats

### Standard Suffixes

- `_total` - Total count (counter)
- `_active` - Active count (gauge)
- `_time` - Duration (histogram)
- `_bytes` - Size (histogram)
- `_5xx`, `_4xx`, `_2xx` - Response codes (counter)

### Example Full Names

```
cluster.backend_service.upstream_rq_total
cluster.backend_service.upstream_rq_5xx
cluster.backend_service.upstream_cx_active
cluster.backend_service.upstream_rq_time
http.ingress.downstream_rq_2xx
listener.0.0.0.0_8080.downstream_cx_total
```

---

## Admin Interface Integration

### Querying Stats

**All stats (text format)**:
```bash
curl http://localhost:9901/stats
```

**Filter by regex**:
```bash
curl http://localhost:9901/stats?filter=cluster\.backend
```

**Prometheus format**:
```bash
curl http://localhost:9901/stats/prometheus
```

**JSON format**:
```bash
curl http://localhost:9901/stats?format=json
```

**Used stats only**:
```bash
curl http://localhost:9901/stats?usedonly=
```

### Output Format

**Text format**:
```
cluster.backend.upstream_rq_total: 12345
cluster.backend.upstream_rq_2xx: 12000
cluster.backend.upstream_rq_5xx: 100
cluster.backend.upstream_cx_active: 50
```

**Histogram format**:
```
cluster.backend.upstream_rq_time: P0(1.2,…) P25(5.5,…) P50(10.3,…) P75(20.1,…)
    P90(45.2,…) P95(67.8,…) P99(120.5,…) P99.9(250.1,…) P100(500.0,…)
```

---

## Debugging

### Recent Lookups

Track which stats are being looked up (useful for finding stat name typos):

```bash
# Enable tracking
curl -X POST http://localhost:9901/stats/recentlookups/enable

# View recent lookups
curl http://localhost:9901/stats/recentlookups

# Clear tracking
curl -X POST http://localhost:9901/stats/recentlookups/clear

# Disable tracking
curl -X POST http://localhost:9901/stats/recentlookups/disable
```

### Memory Usage

Check stats memory consumption:

```bash
curl http://localhost:9901/memory | grep stats
```

### Contention

Check if stats are causing lock contention:

```bash
curl http://localhost:9901/contention
```

---

## Advanced Topics

### Custom Stats Sinks

Implement `Stats::Sink` interface:

```cpp
class MySink : public Stats::Sink {
public:
    void flush(Stats::MetricSnapshot& snapshot) override {
        // Export counters
        for (const auto& counter : snapshot.counters()) {
            exportCounter(counter.get().name(), counter.get().value());
        }

        // Export gauges
        for (const auto& gauge : snapshot.gauges()) {
            exportGauge(gauge.get().name(), gauge.get().value());
        }

        // Export histograms
        for (const auto& histogram : snapshot.histograms()) {
            const auto& stats = histogram.get().cumulativeStatistics();
            exportHistogram(histogram.get().name(), stats);
        }
    }
};
```

### Tag Extraction

Extract dimensions from stat names:

```cpp
// Config: extract "backend" tag from cluster name
stats_config:
  stats_tags:
    - tag_name: backend
      regex: "^cluster\\.(.*?)\\.upstream"
```

**Before**:
```
cluster.backend_v1.upstream_rq_total: 100
cluster.backend_v2.upstream_rq_total: 200
```

**After**:
```
cluster.upstream_rq_total{backend="backend_v1"}: 100
cluster.upstream_rq_total{backend="backend_v2"}: 200
```

---

## See Also

- [OVERVIEW.md](./OVERVIEW.md) - Observability overview
- [admin_interface.md](./admin_interface.md) - Admin endpoints
- `source/docs/stats.md` - Original stats documentation
- `envoy/stats/stats_macros.h` - Stat definition macros
