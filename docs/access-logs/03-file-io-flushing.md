# Part 3: Access Logs — File I/O, Flushing, and Periodic Logs

## File Access Log Architecture

```mermaid
classDiagram
    class AccessLogManager {
        <<interface>>
        +createAccessLog(file_info) AccessLogFileSharedPtr
        +reopen()
    }
    class AccessLogManagerImpl {
        -access_logs_ : map~string, AccessLogFileSharedPtr~
        -api_ : Api
        -dispatcher_ : Dispatcher
        -lock_ : Thread::BasicLockable
        -file_flush_interval_msec_ : uint32_t
        +createAccessLog(file_info) AccessLogFileSharedPtr
        +reopen()
    }
    class AccessLogFileImpl {
        -file_ : Filesystem::FilePtr
        -flush_buffer_ : Buffer
        -flush_thread_ : Thread
        -flush_timer_ : Event::TimerPtr
        -flush_lock_ : Mutex
        -min_flush_size_ : uint64_t
        +write(data) void
        +flush() void
        +reopen() void
    }

    AccessLogManagerImpl ..|> AccessLogManager
    AccessLogManagerImpl --> AccessLogFileImpl : "manages"
```

## File Write and Flush Flow

```mermaid
sequenceDiagram
    participant AL as FileAccessLog
    participant ALF as AccessLogFileImpl
    participant FlushBuf as flush_buffer_
    participant FlushThread as Flush Thread
    participant Disk as File on Disk

    AL->>ALF: write(formatted_entry)
    ALF->>FlushBuf: Append to flush_buffer_ (lock)
    Note over FlushBuf: Data buffered, not yet on disk
    
    alt Buffer >= min_flush_size (64KB default)
        ALF->>FlushThread: Signal flush
    end
    
    alt Flush timer fires (configurable interval)
        FlushThread->>FlushBuf: Swap buffer (lock)
        FlushThread->>Disk: file_->write(buffer)
        Note over Disk: Data written to disk
    end
```

### Write Path Detail

```mermaid
flowchart TD
    A["FileAccessLog::emitLog()"] --> B["formatter_->format(context, info)"]
    B --> C["Formatted string\n(e.g., '2024-01-15 GET /api 200 15ms')"]
    C --> D["log_file_->write(formatted_string)"]
    D --> E["Lock flush_lock_"]
    E --> F["Append to flush_buffer_"]
    F --> G["Unlock"]
    G --> H{Buffer >= min_flush_size?}
    H -->|Yes| I["Signal flush thread"]
    H -->|No| J["Wait for timer"]
    
    I --> K["Flush thread wakes"]
    J --> K
    K --> L["Swap flush_buffer_ (double buffer)"]
    L --> M["file_->write(swapped_buffer)"]
    M --> N["Data on disk"]
```

### Double Buffering

```mermaid
graph LR
    subgraph "Worker Threads"
        W1["Worker 1: write()"]
        W2["Worker 2: write()"]
        W3["Worker 3: write()"]
    end
    
    subgraph "AccessLogFileImpl"
        FB["flush_buffer_\n(accumulates writes)"]
        AB["about_to_write_buffer_\n(being flushed)"]
    end
    
    subgraph "Flush Thread"
        FT["Swap buffers\nWrite to disk"]
    end
    
    W1 --> FB
    W2 --> FB
    W3 --> FB
    FB -->|"swap"| AB
    AB --> FT
```

## File Reopen (Log Rotation)

```mermaid
sequenceDiagram
    participant Admin as Admin API / Signal
    participant ALM as AccessLogManagerImpl
    participant ALF as AccessLogFileImpl
    participant OS as Operating System

    Note over Admin: SIGHUP or POST /reopen_logs
    Admin->>ALM: reopen()
    ALM->>ALM: For each access_log in access_logs_
    ALM->>ALF: reopen()
    ALF->>ALF: Set reopen_file_ = true
    
    Note over ALF: On next write...
    ALF->>ALF: Check reopen_file_ flag
    ALF->>OS: file_->close()
    ALF->>OS: file_->open(path)
    Note over OS: New file created\n(or same path re-opened)
    ALF->>ALF: reopen_file_ = false
```

This enables external log rotation tools (logrotate) to rename the current log file and send SIGHUP to Envoy, which reopens the file at the original path.

## Periodic Access Log Flushing

### How It Works

For long-lived streams (WebSocket, gRPC streaming, HTTP CONNECT tunnels), a single end-of-stream access log entry may come too late. Periodic flushing logs intermediate stats.

```mermaid
sequenceDiagram
    participant AS as ActiveStream
    participant Timer as access_log_flush_timer_
    participant AL as AccessLog::Instance
    participant BM as BytesMeter

    AS->>Timer: Create timer (access_log_flush_interval)
    
    loop Every flush interval
        Timer->>AS: Timer fires
        AS->>AS: Check: requestComplete() not set?
        
        alt Request still in flight
            AS->>BM: takeDownstreamPeriodicLoggingSnapshot(now)
            Note over BM: Capture bytes sent/received delta
            AS->>AL: log(DownstreamPeriodic)
            Note over AL: Intermediate log entry
            AS->>Timer: refreshAccessLogFlushTimer()
        else Request complete
            Note over AS: Skip — final log already done
        end
    end
    
    AS->>AL: log(DownstreamEnd)
    Note over AL: Final log entry with totals
```

### BytesMeter Snapshots

```mermaid
graph TD
    subgraph "BytesMeter"
        Total["Total bytes\n(monotonically increasing)"]
        Snapshot["Periodic snapshot\n(delta since last snapshot)"]
        LastSnapshot["Last snapshot time"]
    end
    
    subgraph "Periodic Log"
        P1["Period 1: 50KB received, 120KB sent"]
        P2["Period 2: 30KB received, 80KB sent"]
        P3["Period 3: 10KB received, 40KB sent"]
    end
    
    subgraph "Final Log"
        F["Total: 90KB received, 240KB sent"]
    end
    
    Total --> P1 --> P2 --> P3 --> F
```

### Configuration

```mermaid
graph TD
    subgraph "HCM Config"
        ALFI["access_log_flush_interval\n(Duration, e.g., 10s)"]
        ALFNR["flush_access_log_on_new_request\n(bool)"]
    end
    
    ALFI -->|"Controls"| Timer["Periodic flush timer\n(DownstreamPeriodic)"]
    ALFNR -->|"Controls"| NewReq["Log on new request\n(DownstreamStart)"]
```

## Log Type Filtering with Periodic Logs

When periodic flushing is enabled, you may want different loggers for different log types:

```mermaid
graph TD
    subgraph "Logger 1: File (all types)"
        L1["FileAccessLog\nNo filter → logs everything"]
    end
    
    subgraph "Logger 2: gRPC (end only)"
        L2["HttpGrpcAccessLog"]
        LTF["LogTypeFilter\ntypes: [DownstreamEnd]"]
        L2 --> LTF
    end
    
    subgraph "Logger 3: Metrics (periodic only)"
        L3["StatsAccessLog"]
        LTF2["LogTypeFilter\ntypes: [DownstreamPeriodic]"]
        L3 --> LTF2
    end
    
    Log["ActiveStream::log(type)"] --> L1
    Log --> L2
    Log --> L3
```

## Access Log Stats

```mermaid
graph TD
    subgraph "AccessLogFileImpl Stats"
        S1["flushed_by_timer\n— flush triggered by timer"]
        S2["reopen_failed\n— file reopen failures"]
        S3["write_buffered\n— writes buffered (not flushed)"]
        S4["write_completed\n— writes completed to disk"]
        S5["write_failed\n— write failures"]
        S6["write_total_buffered\n— total bytes buffered"]
    end
```

## Complete Access Log Pipeline

```mermaid
graph TB
    subgraph "Request Processing"
        AS["ActiveStream"]
    end
    
    subgraph "Log Trigger"
        Trigger{Log type?}
        Trigger -->|DownstreamStart| NewReq["New request"]
        Trigger -->|DownstreamPeriodic| Periodic["Timer flush"]
        Trigger -->|DownstreamEnd| EndReq["Stream complete"]
    end
    
    subgraph "Log Entry Creation"
        Context["Build Formatter::Context\n(headers, trailers, type)"]
        SI["StreamInfo\n(timing, bytes, flags)"]
    end
    
    subgraph "For Each Logger"
        Eval{Filter?}
        Eval -->|Pass| Format["Format entry"]
        Eval -->|Reject| Skip["Skip"]
        
        Format --> Output{Logger type?}
        Output -->|File| File["Write to buffer\n→ flush thread → disk"]
        Output -->|gRPC| GRPC["Build proto\n→ buffer → stream"]
        Output -->|Stdout| STD["Write to stderr/stdout"]
        Output -->|OTLP| OTLP["Build OTLP log\n→ export"]
    end
    
    AS --> Trigger
    Trigger --> Context
    Context --> Eval
    SI --> Format
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `source/common/access_log/access_log_manager_impl.h/cc` | `AccessLogManagerImpl`, `AccessLogFileImpl` | File management, buffered writing |
| `source/extensions/access_loggers/common/file_access_log_impl.h/cc` | `FileAccessLog` | File access log implementation |
| `source/extensions/access_loggers/file/config.cc` | File access log config | Factory for file loggers |
| `source/common/http/conn_manager_impl.cc:812-829` | Periodic flush timer | Timer setup and callback |
| `source/common/http/conn_manager_impl.cc:833` | `ActiveStream::log()` | Main log call site |
| `source/common/formatter/substitution_format_string.h` | `SubstitutionFormatStringUtils` | Format string config parsing |
| `source/common/stream_info/stream_info_impl.h` | `StreamInfoImpl` | Per-request data source |

---

**Previous:** [Part 2 — Formatters, Filters, and gRPC Logs](02-formatters-filters-grpc.md)  
**Back to:** [Part 1 — Architecture & Lifecycle](01-architecture-lifecycle.md)
