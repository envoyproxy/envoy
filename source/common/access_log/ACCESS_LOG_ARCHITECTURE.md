# Envoy Access Log Architecture

## Overview

The Envoy access log subsystem provides a flexible, high-performance mechanism for logging HTTP and TCP traffic. It consists of two main components:

1. **Access Log Filters** - Determine whether a request should be logged based on various criteria
2. **Access Log Manager** - Manages log files and handles asynchronous writing with buffering

---

## System Architecture

```mermaid
graph TB
    subgraph "Access Log Subsystem"
        ALF[AccessLogFactory]
        FM[FilterFactory]
        ALM[AccessLogManagerImpl]

        subgraph "Filters"
            SF[StatusCodeFilter]
            DF[DurationFilter]
            RF[RuntimeFilter]
            HF[HeaderFilter]
            AND[AndFilter]
            OR[OrFilter]
            NHC[NotHealthCheckFilter]
            TF[TraceableRequestFilter]
            RFF[ResponseFlagFilter]
            GSF[GrpcStatusFilter]
            LTF[LogTypeFilter]
            MF[MetadataFilter]
        end

        subgraph "File Management"
            ALFI[AccessLogFileImpl]
            FB[Flush Buffer]
            FT[Flush Thread]
            FS[File System]
        end
    end

    Config[Protobuf Config] --> ALF
    Config --> FM
    ALF --> Filters
    FM --> Filters

    ALM --> ALFI
    ALFI --> FB
    FB --> FT
    FT --> FS

    Request[Request/Response] --> Filters
    Filters -->|Pass| ALFI
    Filters -->|Reject| Drop[Drop Log Entry]
```

---

## Component Details

### 1. Filter System

The filter system provides a composable way to determine which requests should be logged.

#### Filter Class Hierarchy

```mermaid
classDiagram
    class Filter {
        <<interface>>
        +evaluate(context, info) bool
    }

    class ComparisonFilter {
        -config_: ComparisonFilter
        -runtime_: Runtime::Loader
        +compareAgainstValue(lhs) bool
    }

    class StatusCodeFilter {
        +evaluate(context, info) bool
    }

    class DurationFilter {
        +evaluate(context, info) bool
    }

    class OperatorFilter {
        -filters_: vector~FilterPtr~
    }

    class AndFilter {
        +evaluate(context, info) bool
    }

    class OrFilter {
        +evaluate(context, info) bool
    }

    class RuntimeFilter {
        -runtime_: Runtime::Loader
        -random_: RandomGenerator
        -runtime_key_: string
        -percent_: FractionalPercent
        +evaluate(context, info) bool
    }

    class HeaderFilter {
        -header_data_: HeaderDataPtr
        +evaluate(context, info) bool
    }

    class ResponseFlagFilter {
        -configured_flags_: vector~bool~
        +evaluate(context, info) bool
    }

    class GrpcStatusFilter {
        -statuses_: GrpcStatusHashSet
        -exclude_: bool
        +evaluate(context, info) bool
    }

    class NotHealthCheckFilter {
        +evaluate(context, info) bool
    }

    class TraceableRequestFilter {
        +evaluate(context, info) bool
    }

    class LogTypeFilter {
        -types_: LogTypeHashSet
        -exclude_: bool
        +evaluate(context, info) bool
    }

    class MetadataFilter {
        -present_matcher_: PresentMatcher
        -value_matcher_: ValueMatcherPtr
        -path_: vector~string~
        -default_match_: bool
        +evaluate(context, info) bool
    }

    Filter <|-- ComparisonFilter
    Filter <|-- OperatorFilter
    Filter <|-- RuntimeFilter
    Filter <|-- HeaderFilter
    Filter <|-- ResponseFlagFilter
    Filter <|-- GrpcStatusFilter
    Filter <|-- NotHealthCheckFilter
    Filter <|-- TraceableRequestFilter
    Filter <|-- LogTypeFilter
    Filter <|-- MetadataFilter

    ComparisonFilter <|-- StatusCodeFilter
    ComparisonFilter <|-- DurationFilter
    OperatorFilter <|-- AndFilter
    OperatorFilter <|-- OrFilter
```

#### Filter Types

| Filter Type | Purpose | Key Logic |
|------------|---------|-----------|
| **StatusCodeFilter** | Filter based on HTTP response code | Compares response code against configured value (GE, EQ, LE, NE) |
| **DurationFilter** | Filter based on request duration | Compares request duration in milliseconds against configured value |
| **RuntimeFilter** | Probabilistic filtering using runtime flags | Uses random sampling with configurable percentage |
| **HeaderFilter** | Filter based on request/response headers | Matches headers using configured patterns |
| **AndFilter** | Logical AND of sub-filters | All sub-filters must evaluate to true |
| **OrFilter** | Logical OR of sub-filters | At least one sub-filter must evaluate to true |
| **NotHealthCheckFilter** | Exclude health check requests | Returns true if request is NOT a health check |
| **TraceableRequestFilter** | Include only traceable requests | Checks if request was service-forced traced |
| **ResponseFlagFilter** | Filter based on Envoy response flags | Checks if specific response flags are set |
| **GrpcStatusFilter** | Filter based on gRPC status codes | Matches or excludes specific gRPC status codes |
| **LogTypeFilter** | Filter based on access log type | Matches or excludes specific log types |
| **MetadataFilter** | Filter based on dynamic metadata | Matches metadata keys and values |

---

### 2. Filter Evaluation Flow

```mermaid
sequenceDiagram
    participant Request
    participant FilterFactory
    participant Filter
    participant AccessLog

    Request->>FilterFactory: Create filter from config
    FilterFactory->>Filter: Instantiate filter

    Note over Request,AccessLog: For each request/response

    Request->>Filter: evaluate(context, info)

    alt StatusCodeFilter
        Filter->>Filter: Get response code
        Filter->>Filter: compareAgainstValue(code)
        Filter-->>Request: true/false
    else RuntimeFilter
        Filter->>Filter: Get/generate random value
        Filter->>Filter: Check runtime feature flag
        Filter-->>Request: true/false
    else AndFilter
        loop For each sub-filter
            Filter->>Filter: sub_filter.evaluate()
            alt Any returns false
                Filter-->>Request: false (short-circuit)
            end
        end
        Filter-->>Request: true
    else OrFilter
        loop For each sub-filter
            Filter->>Filter: sub_filter.evaluate()
            alt Any returns true
                Filter-->>Request: true (short-circuit)
            end
        end
        Filter-->>Request: false
    end

    alt Filter passes
        Request->>AccessLog: Write log entry
    else Filter rejects
        Request->>Request: Drop log entry
    end
```

---

### 3. Access Log Manager

The `AccessLogManagerImpl` manages multiple log files and coordinates writing.

```mermaid
classDiagram
    class AccessLogManager {
        <<interface>>
        +reopen() void
        +createAccessLog(file_info) StatusOr~AccessLogFileSharedPtr~
    }

    class AccessLogManagerImpl {
        -file_flush_interval_msec_: milliseconds
        -file_min_flush_size_kb_: uint64_t
        -api_: Api&
        -dispatcher_: Dispatcher&
        -lock_: BasicLockable&
        -file_stats_: AccessLogFileStats
        -access_logs_: node_hash_map~string, AccessLogFileSharedPtr~
        +reopen() void
        +createAccessLog(file_info) StatusOr~AccessLogFileSharedPtr~
    }

    class AccessLogFile {
        <<interface>>
        +write(data) void
        +reopen() void
        +flush() void
    }

    class AccessLogFileImpl {
        -file_: FilePtr
        -file_lock_: BasicLockable&
        -flush_lock_: MutexBasicLockable
        -write_lock_: MutexBasicLockable
        -flush_thread_: ThreadPtr
        -flush_event_: CondVar
        -flush_buffer_: OwnedImpl
        -about_to_write_buffer_: OwnedImpl
        -flush_timer_: TimerPtr
        -min_flush_size_: uint64_t
        +write(data) void
        +reopen() void
        +flush() void
        -doWrite(buffer) void
        -flushThreadFunc() void
        -createFlushStructures() void
    }

    class AccessLogFileStats {
        +flushed_by_timer: Counter
        +reopen_failed: Counter
        +write_buffered: Counter
        +write_completed: Counter
        +write_failed: Counter
        +write_total_buffered: Gauge
    }

    AccessLogManager <|-- AccessLogManagerImpl
    AccessLogFile <|-- AccessLogFileImpl
    AccessLogManagerImpl --> AccessLogFileImpl: manages
    AccessLogFileImpl --> AccessLogFileStats: tracks
```

---

### 4. Threading Model

The `AccessLogFileImpl` uses a sophisticated multi-threaded design for high performance without blocking worker threads.

```mermaid
graph TB
    subgraph "Worker Threads (Multiple)"
        WT1[Worker Thread 1]
        WT2[Worker Thread 2]
        WTN[Worker Thread N]
    end

    subgraph "AccessLogFileImpl"
        WL[write_lock_]
        FB[flush_buffer_]
        FE[flush_event_]

        subgraph "Flush Thread"
            FL[flush_lock_]
            ATW[about_to_write_buffer_]
            FTF[flushThreadFunc]
        end

        subgraph "Disk I/O"
            FILEL[file_lock_]
            DW[doWrite]
            FS[File System]
        end
    end

    WT1 -->|1. Lock write_lock_| WL
    WT2 -->|1. Lock write_lock_| WL
    WTN -->|1. Lock write_lock_| WL

    WT1 -->|2. Append data| FB
    WT2 -->|2. Append data| FB
    WTN -->|2. Append data| FB

    FB -->|3. Notify if size > threshold| FE
    FE -->|4. Wake up| FTF

    FTF -->|5. Lock write_lock_| WL
    FTF -->|6. Move data| FB
    FB -.->|Data moved| ATW
    FTF -->|7. Unlock write_lock_| WL

    FTF -->|8. Lock flush_lock_| FL
    FTF -->|9. Lock file_lock_| FILEL
    ATW -->|10. Write| DW
    DW -->|11. Write to disk| FS
    FTF -->|12. Unlock file_lock_| FILEL
    FTF -->|13. Unlock flush_lock_| FL

    style WT1 fill:#e1f5ff
    style WT2 fill:#e1f5ff
    style WTN fill:#e1f5ff
    style FTF fill:#ffe1f5
    style FS fill:#f0f0f0
```

#### Lock Hierarchy

The locks are always acquired in this order to prevent deadlock:

1. **write_lock_** - Protects flush_buffer_ and coordination flags
2. **flush_lock_** - Protects about_to_write_buffer_ and flush operations
3. **file_lock_** - Cross-process lock for actual disk writes

```mermaid
graph LR
    A[write_lock_] --> B[flush_lock_]
    B --> C[file_lock_]

    style A fill:#ffcccc
    style B fill:#ccffcc
    style C fill:#ccccff
```

---

### 5. Write Flow

```mermaid
sequenceDiagram
    participant WT as Worker Thread
    participant WL as write_lock_
    participant FB as flush_buffer_
    participant FE as flush_event_
    participant FT as Flush Thread
    participant FL as flush_lock_
    participant ATW as about_to_write_buffer_
    participant FileLock as file_lock_
    participant Disk as File System

    WT->>WL: Lock
    WT->>FB: Append log data
    WT->>WT: Check buffer size

    alt Buffer size > min_flush_size
        WT->>FE: notifyOne()
    end

    WT->>WL: Unlock

    Note over FT: Waiting on flush_event

    FE->>FT: Wake up
    FT->>WL: Lock write_lock_
    FT->>FL: Lock flush_lock_
    FT->>ATW: Move data from flush_buffer_
    FB-->>ATW: Data transferred
    FT->>WL: Unlock write_lock_

    Note over WT: Can continue writing

    FT->>FileLock: Lock file_lock_

    loop For each buffer slice
        FT->>Disk: Write slice
        Disk-->>FT: Result
    end

    FT->>FileLock: Unlock file_lock_
    FT->>FL: Unlock flush_lock_
    FT->>ATW: Drain buffer

    Note over FT: Wait for next event
```

---

### 6. Flush Triggers

The flush thread is triggered by multiple conditions:

```mermaid
graph TD
    Start[Flush Thread Waiting]

    T1[Timer Expires]
    T2[Buffer Size > Threshold]
    T3[Reopen Request]
    T4[Explicit flush Call]
    T5[Thread Exit Signal]

    T1 --> Wake[Wake Flush Thread]
    T2 --> Wake
    T3 --> Wake
    T4 --> Sync[Synchronous Flush]
    T5 --> Exit[Thread Exit]

    Wake --> Check{Check Conditions}
    Check -->|Exit Signal| Exit
    Check -->|Reopen Flag| Reopen[Close and Reopen File]
    Check -->|Has Data| Transfer[Transfer to about_to_write_buffer_]

    Reopen --> Transfer
    Transfer --> Write[Write to Disk]
    Write --> Start

    Sync --> Lock[Lock flush_lock_]
    Lock --> DirectWrite[Direct Write to Disk]
    DirectWrite --> Unlock[Unlock]

    style Wake fill:#ffeb99
    style Write fill:#99ebff
    style Exit fill:#ff9999
```

---

### 7. File Reopening Mechanism

File reopening is used for log rotation without blocking workers:

```mermaid
sequenceDiagram
    participant Signal as External Signal
    participant ALM as AccessLogManagerImpl
    participant ALFI as AccessLogFileImpl
    participant WL as write_lock_
    participant FT as Flush Thread

    Signal->>ALM: reopen()

    loop For each access log
        ALM->>ALFI: reopen()
        ALFI->>WL: Lock
        ALFI->>ALFI: Set reopen_file_ = true
        ALFI->>FT: flush_event_.notifyOne()
        ALFI->>WL: Unlock
    end

    Note over FT: Wake up on event

    FT->>WL: Lock write_lock_
    FT->>FT: Check reopen_file_
    FT->>FT: Copy flag to local var
    FT->>FT: Clear reopen_file_
    FT->>WL: Unlock write_lock_

    alt File is open
        FT->>FT: Close file
    end

    FT->>FT: Open file with default flags

    alt Open failed
        FT->>FT: Increment reopen_failed stat
        Note over FT: Will retry on next event
    else Open succeeded
        FT->>FT: Clear do_reopen flag
    end

    FT->>FT: Continue with write operation
```

---

### 8. Factory Pattern

Both filters and access log instances use the factory pattern:

```mermaid
graph TB
    subgraph "Configuration"
        PB[Protobuf Config]
    end

    subgraph "Factory Layer"
        FF[FilterFactory::fromProto]
        ALF[AccessLogFactory::fromProto]
    end

    subgraph "Runtime Instances"
        F[Filter Instance]
        ALI[AccessLog Instance]
    end

    PB -->|Access Log Filter Config| FF
    PB -->|Access Log Config| ALF

    FF -->|Create based on type| F
    ALF -->|1. Create filter| FF
    FF -->|Return filter| ALF
    ALF -->|2. Get factory| Factory[AccessLogInstanceFactory]
    Factory -->|3. Create instance| ALI

    style PB fill:#e1e1e1
    style FF fill:#99ccff
    style ALF fill:#99ccff
    style F fill:#99ff99
    style ALI fill:#99ff99
```

#### Filter Factory Logic

```mermaid
flowchart TD
    Start[FilterFactory::fromProto] --> Check{Check filter_specifier}

    Check -->|kStatusCodeFilter| SC[Create StatusCodeFilter]
    Check -->|kDurationFilter| DF[Create DurationFilter]
    Check -->|kNotHealthCheckFilter| NHC[Create NotHealthCheckFilter]
    Check -->|kTraceableFilter| TF[Create TraceableRequestFilter]
    Check -->|kRuntimeFilter| RF[Create RuntimeFilter]
    Check -->|kAndFilter| AF[Create AndFilter]
    Check -->|kOrFilter| OF[Create OrFilter]
    Check -->|kHeaderFilter| HF[Create HeaderFilter]
    Check -->|kResponseFlagFilter| RFF[Create ResponseFlagFilter]
    Check -->|kGrpcStatusFilter| GSF[Create GrpcStatusFilter]
    Check -->|kMetadataFilter| MF[Create MetadataFilter]
    Check -->|kLogTypeFilter| LTF[Create LogTypeFilter]
    Check -->|kExtensionFilter| EF[Get Extension Factory]

    EF --> EFC[Extension Factory Create]

    SC --> Return[Return FilterPtr]
    DF --> Return
    NHC --> Return
    TF --> Return
    RF --> Return
    AF --> Return
    OF --> Return
    HF --> Return
    RFF --> Return
    GSF --> Return
    MF --> Return
    LTF --> Return
    EFC --> Return
```

---

### 9. Statistics Tracking

The system tracks various statistics for monitoring:

```mermaid
graph LR
    subgraph "AccessLogFileStats"
        FC[flushed_by_timer]
        RF[reopen_failed]
        WB[write_buffered]
        WC[write_completed]
        WF[write_failed]
        WTB[write_total_buffered]
    end

    subgraph "Events"
        Timer[Timer Fires] --> FC
        ReopenFail[Reopen Fails] --> RF
        Write[write Called] --> WB
        Write --> WTB
        DiskWrite[Disk Write Success] --> WC
        DiskFail[Disk Write Fail] --> WF
        Flush[Data Flushed] --> WTB
    end

    style FC fill:#ccffcc
    style RF fill:#ffcccc
    style WB fill:#ccccff
    style WC fill:#ccffcc
    style WF fill:#ffcccc
    style WTB fill:#ffffcc
```

---

### 10. Comparison Filter Operators

The `ComparisonFilter` base class supports multiple comparison operators:

```mermaid
graph TD
    Start[compareAgainstValue lhs] --> GetValue[Get configured value]
    GetValue --> Runtime{Has runtime key?}

    Runtime -->|Yes| GetRuntime[Get runtime value]
    Runtime -->|No| UseDefault[Use default value]

    GetRuntime --> Compare
    UseDefault --> Compare

    Compare{Operator Type}
    Compare -->|GE| GE[lhs >= value]
    Compare -->|EQ| EQ[lhs == value]
    Compare -->|LE| LE[lhs <= value]
    Compare -->|NE| NE[lhs != value]

    GE --> Return[Return bool]
    EQ --> Return
    LE --> Return
    NE --> Return

    style GE fill:#e1f5e1
    style EQ fill:#e1e1f5
    style LE fill:#f5e1e1
    style NE fill:#f5f5e1
```

---

### 11. Buffer Management Strategy

The dual-buffer design minimizes lock contention:

```mermaid
graph TB
    subgraph "Worker Threads Domain"
        WT[Worker Threads]
        WL[write_lock_]
        FB[flush_buffer_]
    end

    subgraph "Flush Thread Domain"
        FT[Flush Thread]
        FL[flush_lock_]
        ATW[about_to_write_buffer_]
    end

    subgraph "Disk I/O Domain"
        FileLock[file_lock_]
        Disk[Disk I/O]
    end

    WT -->|Lock & Write| WL
    WL -->|Protects| FB

    FB -.->|Move data quickly| ATW

    FT -->|Lock & Write| FL
    FL -->|Protects| ATW

    ATW -->|Write with| FileLock
    FileLock -->|Serialize| Disk

    Note1[Workers write to flush_buffer_<br/>with minimal lock time]
    Note2[Flush thread moves data quickly<br/>then unlocks write_lock_]
    Note3[Disk I/O happens without<br/>blocking workers]

    style WT fill:#e1f5ff
    style FT fill:#ffe1f5
    style Disk fill:#f0f0f0
```

---

### 12. Key Design Patterns

#### Pattern 1: Double Buffering
- Workers write to `flush_buffer_`
- Flush thread quickly moves data to `about_to_write_buffer_`
- Disk I/O happens from `about_to_write_buffer_` without blocking workers

#### Pattern 2: Lazy Initialization
- Flush thread is created on first write
- Reduces resource usage for unused log files

#### Pattern 3: Condition Variable for Coordination
- Flush thread waits on `flush_event_`
- Woken by: timer, buffer threshold, reopen request, or exit signal
- Avoids busy-waiting and provides good responsiveness

#### Pattern 4: Cross-Process File Locking
- `file_lock_` is provided externally (process-wide)
- Enables safe log writing during hot restart
- Multiple processes can write to same file without corruption

#### Pattern 5: Factory Pattern with Type Registration
- Filters and access logs use factory pattern
- Extensible through plugin mechanism
- Configuration-driven instantiation

---

## Performance Characteristics

### Write Path Latency

```mermaid
graph LR
    A[Worker Thread] -->|~100ns| B[Acquire write_lock_]
    B -->|~50ns| C[Append to buffer]
    C -->|~50ns| D[Check size]
    D -->|~50ns| E[Unlock write_lock_]

    E -.->|Async| F[Flush Thread]
    F -.->|Background| G[Disk I/O]

    style A fill:#e1f5ff
    style E fill:#e1f5ff
    style F fill:#ffe1f5
    style G fill:#f0f0f0
```

- **Worker thread latency**: ~250ns (fast path)
- **No blocking on disk I/O**: Workers never wait for disk
- **Batched writes**: Multiple log entries flushed together
- **Minimal lock contention**: Separate locks for different operations

---

## Error Handling

```mermaid
flowchart TD
    Write[Write Operation] --> DiskWrite{Disk Write}

    DiskWrite -->|Success| IncSuccess[Increment write_completed]
    DiskWrite -->|Failure| IncFail[Increment write_failed]

    IncFail --> Continue[Continue Operation]
    IncSuccess --> Continue

    Reopen[Reopen Operation] --> TryOpen{Try Open}
    TryOpen -->|Success| ClearFlag[Clear reopen flag]
    TryOpen -->|Failure| IncReopen[Increment reopen_failed]

    IncReopen --> Retry[Retry on next event]
    ClearFlag --> Normal[Normal operation]

    style IncFail fill:#ffcccc
    style IncReopen fill:#ffcccc
    style IncSuccess fill:#ccffcc
    style ClearFlag fill:#ccffcc
```

---

## Configuration Example

```yaml
access_log:
- name: envoy.access_loggers.file
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
    path: /var/log/envoy/access.log
    filter:
      and_filter:
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
        - not_health_check_filter: {}
```

This configuration logs requests that:
- Have status code >= 400 AND
- Duration >= 1000ms AND
- Are not health checks

---

## Summary

The Envoy access log system is designed for:

1. **High Performance**: Async I/O, buffering, minimal lock contention
2. **Flexibility**: Composable filters, pluggable backends
3. **Reliability**: Cross-process safe, graceful error handling
4. **Observability**: Rich statistics tracking

The architecture separates concerns:
- **Filters** decide what to log
- **Manager** coordinates multiple logs
- **File Implementation** handles performant writing

This design allows Envoy to log millions of requests per second without impacting request processing latency.
