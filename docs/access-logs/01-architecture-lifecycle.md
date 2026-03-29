# Part 1: Access Logs — Architecture & Lifecycle

## Overview

Envoy's access log system records information about every request/connection processed. It supports multiple output formats (text, JSON), multiple destinations (file, gRPC, stdout, OpenTelemetry), configurable filters, and periodic flushing during long-lived streams. Access logs use the `StreamInfo` object as their primary data source.

## Access Log Architecture

```mermaid
graph TB
    subgraph "Configuration"
        Proto["AccessLog proto config"]
        Proto --> Filter["AccessLog::Filter\n(optional)"]
        Proto --> Factory["AccessLogInstanceFactory"]
        Factory --> Instance["AccessLog::Instance\n(file, gRPC, OTLP, etc.)"]
    end
    
    subgraph "Runtime"
        HCM["ConnectionManagerImpl"]
        AS["ActiveStream"]
        FM["FilterManager"]
        SI["StreamInfo"]
    end
    
    subgraph "Output"
        File["File (disk)"]
        GRPC["gRPC ALS"]
        Stdout["stdout/stderr"]
        OTLP["OpenTelemetry"]
        Fluentd["Fluentd"]
    end
    
    AS -->|"log(type)"| Instance
    Instance -->|"evaluate filter"| Filter
    Filter -->|"pass"| Instance
    Instance -->|"format()"| SI
    Instance --> File
    Instance --> GRPC
    Instance --> Stdout
    Instance --> OTLP
    Instance --> Fluentd
```

## Class Hierarchy

```mermaid
classDiagram
    class AccessLog_Filter {
        <<interface>>
        +evaluate(context, stream_info) bool
    }
    class AccessLog_Instance {
        <<interface>>
        +log(context, stream_info) void
    }
    class ImplBase {
        -filter_ : FilterPtr
        +log(context, stream_info)
        #emitLog(context, stream_info)*
    }
    class FileAccessLog {
        -log_file_ : AccessLogFileSharedPtr
        -formatter_ : FormatterPtr
        +emitLog(context, stream_info)
    }
    class HttpGrpcAccessLog {
        -logger_ : GrpcAccessLogger
        +emitLog(context, stream_info)
    }
    class StreamAccessLog {
        -stream_ : ostream
        -formatter_ : FormatterPtr
        +emitLog(context, stream_info)
    }
    class AccessLogManager {
        <<interface>>
        +createAccessLog(file_info) AccessLogFileSharedPtr
        +reopen()
    }

    ImplBase ..|> AccessLog_Instance
    FileAccessLog --|> ImplBase
    HttpGrpcAccessLog --|> ImplBase
    StreamAccessLog --|> ImplBase
    ImplBase --> AccessLog_Filter : "optional"
```

## When Access Logs Are Written

```mermaid
sequenceDiagram
    participant Client
    participant HCM as ConnectionManagerImpl
    participant AS as ActiveStream
    participant FM as FilterManager
    participant AL as AccessLog::Instance

    Client->>HCM: HTTP Request
    HCM->>AS: newStream()
    
    alt Flush on new request (configured)
        AS->>AL: log(DownstreamStart)
    end
    
    Note over AS: Request processing...
    
    alt Periodic flush enabled
        AS->>AS: access_log_flush_timer fires
        AS->>AL: log(DownstreamPeriodic)
        Note over AL: In-flight metrics logged
    end
    
    alt Tunnel established
        AS->>AL: log(DownstreamTunnelSuccessfullyEstablished)
    end
    
    Note over AS: Response sent, stream complete
    AS->>AS: completeRequest()
    AS->>FM: onStreamComplete() on all filters
    AS->>AL: log(DownstreamEnd)
    Note over AL: Final access log entry
```

### Access Log Types

```mermaid
graph TD
    subgraph "AccessLogType enum"
        DSEnd["DownstreamEnd\n— stream complete (always)"]
        DSPeriodic["DownstreamPeriodic\n— timer-based in-flight"]
        DSStart["DownstreamStart\n— on new request"]
        DSTunnel["DownstreamTunnelSuccessfullyEstablished\n— tunnel established"]
        TCP["TcpConnectionEnd\n— TCP proxy connection end"]
        UDPTunnel["UdpTunnelUpstreamConnected\n— UDP tunnel"]
    end
```

## Access Log Creation Flow

```mermaid
sequenceDiagram
    participant Config as HCM Config
    participant ALF as AccessLogFactory
    participant FF as FilterFactory
    participant AIF as AccessLogInstanceFactory
    participant AL as AccessLog::Instance

    Config->>ALF: fromProto(access_log_config, context)
    
    alt Filter configured
        ALF->>FF: fromProto(config.filter())
        FF-->>ALF: AccessLog::Filter
    end
    
    ALF->>ALF: getAndCheckFactory(config)
    ALF->>AIF: createAccessLogInstance(config, filter, context)
    AIF-->>ALF: AccessLog::Instance
    ALF-->>Config: Instance stored in access_logs_
```

```
File: source/common/access_log/access_log_impl.cc (line 284)

AccessLogFactory::fromProto():
    1. Parse filter (if present) via FilterFactory::fromProto()
    2. Look up AccessLogInstanceFactory by typed config
    3. factory.createAccessLogInstance(config, filter, context)
    4. Return Instance
```

## Log Execution Flow

```mermaid
flowchart TD
    A["ActiveStream::log(type)"] --> B["Build Formatter::Context\n(headers, trailers, log type, span)"]
    B --> C["filter_manager_.log(context)\n→ per-filter access log handlers"]
    C --> D["For each config access_logger:\naccess_logger->log(context, stream_info)"]
    D --> E["ImplBase::log()"]
    E --> F{Filter present?}
    F -->|Yes| G["filter_->evaluate(context, info)"]
    G -->|Pass| H["emitLog(context, stream_info)"]
    G -->|Reject| I["Skip this entry"]
    F -->|No| H
    H --> J["Format and write"]
```

## StreamInfo — Data Source for Logs

```mermaid
graph TD
    SI["StreamInfo"]
    
    subgraph "Timing"
        T1["startTime()"]
        T2["requestComplete()"]
        T3["currentDuration()"]
        T4["downstreamTiming()"]
    end
    
    subgraph "Request/Response"
        R1["responseCode()"]
        R2["responseCodeDetails()"]
        R3["responseFlags()\n(NR, UF, UO, RL, etc.)"]
        R4["protocol()\n(HTTP/1.1, HTTP/2)"]
    end
    
    subgraph "Bytes"
        B1["bytesReceived()"]
        B2["bytesSent()"]
        B3["getDownstreamBytesMeter()"]
        B4["getUpstreamBytesMeter()"]
    end
    
    subgraph "Connection"
        C1["downstreamAddressProvider()\n→ remote/local addresses"]
        C2["upstreamInfo()\n→ host, cluster, timing"]
        C3["downstreamSslConnection()"]
    end
    
    subgraph "Metadata"
        M1["dynamicMetadata()"]
        M2["filterState()"]
        M3["routeEntry()"]
    end
    
    SI --> T1 & T2 & T3 & T4
    SI --> R1 & R2 & R3 & R4
    SI --> B1 & B2 & B3 & B4
    SI --> C1 & C2 & C3
    SI --> M1 & M2 & M3
```

## Where Access Logs Are Configured

```mermaid
graph TD
    subgraph "Configuration Points"
        HCM["HTTP Connection Manager\n→ per-stream HTTP logs"]
        TCPProxy["TCP Proxy\n→ per-connection TCP logs"]
        Listener["Listener\n→ listener-level logs"]
        Route["Route\n→ upstream access logs"]
        Filter["HTTP Filters\n→ addAccessLogHandler()"]
    end
    
    HCM --> AL["AccessLog::Instance list"]
    TCPProxy --> AL
    Listener --> AL
    Route --> AL
    Filter --> AL
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `envoy/access_log/access_log.h` | `Filter`, `Instance`, `AccessLogManager` | Access log interfaces |
| `source/common/access_log/access_log_impl.cc:284` | `AccessLogFactory::fromProto()` | Creates loggers from config |
| `source/common/access_log/access_log_impl.cc:55` | `FilterFactory::fromProto()` | Creates log filters |
| `source/extensions/access_loggers/common/access_log_base.h` | `ImplBase` | Base with filter + emitLog() |
| `source/common/http/conn_manager_impl.cc:833` | `ActiveStream::log()` | HCM log call site |
| `source/common/stream_info/stream_info_impl.h` | `StreamInfoImpl` | Per-request metadata |

---

**Next:** [Part 2 — Formatters, Filters, and gRPC Logs](02-formatters-filters-grpc.md)
