# Dynamic Modules Overview — Part 2: HTTP Filter and Other Extensions

## Series Navigation

| Part | Topic |
|------|-------|
| Part 1 | [Architecture and ABI](./OVERVIEW_PART1_architecture_and_abi.md) |
| **Part 2** | **HTTP Filter and Other Extensions** (this document) |
| Part 3 | [SDKs and Development Guide](./OVERVIEW_PART3_sdks_and_development.md) |
| Part 4 | [Callbacks, Metrics, Advanced Topics](./OVERVIEW_PART4_callbacks_metrics_advanced.md) |

---

## HTTP Filter — The Primary Extension

### End-to-End Flow

```mermaid
sequenceDiagram
    participant Client as Downstream Client
    participant HCM as HttpConnectionManager
    participant Filter as DynamicModuleHttpFilter
    participant Module as .so Module
    participant Upstream as Upstream Cluster

    Client->>HCM: HTTP Request
    HCM->>Filter: decodeHeaders(headers, end_stream)
    Filter->>Module: on_http_filter_request_headers(filter_ptr, headers, end_stream)
    Module-->>Filter: HeadersStatus::Continue

    HCM->>Filter: decodeData(body, end_stream)
    Filter->>Module: on_http_filter_request_body(filter_ptr, body, end_stream)
    Module-->>Filter: BodyStatus::Continue

    Filter->>HCM: forward to upstream
    HCM->>Upstream: HTTP Request

    Upstream-->>HCM: HTTP Response
    HCM->>Filter: encodeHeaders(headers, end_stream)
    Filter->>Module: on_http_filter_response_headers(filter_ptr, headers, end_stream)
    Module-->>Filter: HeadersStatus::Continue

    HCM->>Filter: encodeData(body, end_stream)
    Filter->>Module: on_http_filter_response_body(filter_ptr, body, end_stream)
    Module-->>Filter: BodyStatus::Continue

    Filter->>Client: HTTP Response

    HCM->>Filter: onStreamComplete()
    Filter->>Module: on_http_filter_stream_complete(filter_ptr)

    HCM->>Filter: onDestroy()
    Filter->>Module: on_http_filter_destroy(filter_ptr)
```

### Filter Status Return Values

```mermaid
flowchart TD
    subgraph HeadersStatus["HeadersStatus"]
        HC["Continue\n(proceed to next filter)"]
        HS["StopIteration\n(pause, wait for continueDecoding)"]
        HSB["StopAllIterationAndBuffer\n(buffer all subsequent data)"]
        HSW["StopAllIterationAndWatermark\n(buffer with watermark limit)"]
    end

    subgraph BodyStatus["BodyStatus"]
        BC["Continue\n(proceed to next filter)"]
        BSB["StopIterationAndBuffer\n(buffer body, wait)"]
        BSW["StopIterationAndWatermark\n(buffer with watermark)"]
        BSN["StopIterationNoBuffer\n(stop, don't buffer)"]
    end

    subgraph TrailersStatus["TrailersStatus"]
        TC["Continue"]
        TS["StopIteration"]
    end
```

### Local Reply (Short Circuit)

```mermaid
sequenceDiagram
    participant Client as Client
    participant Filter as DynamicModuleHttpFilter
    participant Module as .so Module

    Client->>Filter: decodeHeaders(request)
    Filter->>Module: on_http_filter_request_headers(...)
    
    Note over Module: Module decides to reject request
    Module->>Filter: callback: sendLocalReply(403, "Forbidden")
    Filter->>Filter: sent_local_reply_ = true
    
    Note over Filter: encodeHeaders/encodeData skipped for local reply body
    
    Filter->>Module: on_http_filter_local_reply(filter_ptr, code)
    Filter->>Client: HTTP 403 Forbidden

    Filter->>Module: on_http_filter_stream_complete(filter_ptr)
    Filter->>Module: on_http_filter_destroy(filter_ptr)
```

### Re-entrancy Protection

```mermaid
flowchart TD
    SendLR["Module calls sendLocalReply()"] --> Flag["sent_local_reply_ = true"]
    Flag --> Encode["Envoy calls encodeHeaders()\nfor the local reply body"]
    Encode --> Check{"sent_local_reply_\n== true?"}
    Check -->|Yes| Skip["Skip calling module\n(avoid re-entrancy)"]
    Check -->|No| Normal["Call module normally"]
```

---

## HTTP Callouts — Async Sub-Requests

Modules can make HTTP requests to other clusters during request processing:

```mermaid
flowchart TD
    subgraph SimpleCallout["Simple HTTP Callout"]
        SC1["Module calls httpCallout(cluster, request, timeout)"]
        SC2["Envoy sends request to cluster"]
        SC3["Response received"]
        SC4["on_http_filter_http_callout_done(filter, id, result)"]
    end

    subgraph StreamCallout["Streaming HTTP Callout"]
        ST1["Module calls startHttpStream(cluster, headers, timeout)"]
        ST2["Module calls sendStreamData(data, end_stream)"]
        ST3["on_http_filter_http_stream_headers(filter, stream_id, headers)"]
        ST4["on_http_filter_http_stream_data(filter, stream_id, data)"]
        ST5["on_http_filter_http_stream_complete(filter, stream_id)"]
    end

    SC1 --> SC2 --> SC3 --> SC4
    ST1 --> ST2 --> ST3 --> ST4 --> ST5
```

### Callout Result Types

| Result | Meaning |
|--------|---------|
| `Success` | Full response received |
| `Failure` | Connection/timeout failure |
| `Reset` | Stream was reset |

---

## Network Filter Extension

```mermaid
flowchart TD
    subgraph NetworkFilter["Network Filter Dynamic Module"]
        NFC["filter_config.h\n(DynamicModuleNetworkFilterConfig)"]
        NF["filter.h\n(DynamicModuleNetworkFilter)"]
        NFactory["factory.h"]
    end

    subgraph Hooks["Network Filter Event Hooks"]
        on_new["on_network_filter_new"]
        on_data["on_network_filter_data\n(read and write)"]
        on_event["on_network_filter_event\n(connected, closed, etc.)"]
        on_destroy["on_network_filter_destroy"]
    end

    NFC --> NF --> Hooks
```

### Network vs HTTP Filter

| Aspect | HTTP Filter | Network Filter |
|--------|------------|----------------|
| Layer | L7 (HTTP) | L4 (TCP/raw bytes) |
| Data unit | Headers + Body + Trailers | Raw byte buffers |
| Callbacks | Request/Response split | Read/Write direction |
| Protocol awareness | Full HTTP semantics | Protocol-agnostic |

---

## Listener Filter Extension

```mermaid
flowchart TD
    subgraph ListenerFilter["Listener Filter Dynamic Module"]
        LFC["filter_config.h"]
        LF["filter.h"]
        LFactory["factory.h"]
    end

    subgraph Hooks["Listener Filter Event Hooks"]
        on_accept["on_listener_filter_new\n(connection accepted)"]
        on_data["on_listener_filter_on_data\n(peek at initial bytes)"]
        on_destroy["on_listener_filter_destroy"]
    end

    LFC --> LF --> Hooks
```

---

## UDP Filter Extension

```mermaid
flowchart TD
    subgraph UDPFilter["UDP Filter Dynamic Module"]
        UFC["filter_config.h"]
        UF["filter.h"]
        UFactory["factory.h"]
        UABI["abi_impl.h"]
    end

    subgraph Hooks["UDP Filter Event Hooks"]
        on_new["on_udp_filter_new"]
        on_data["on_udp_filter_data\n(datagram received)"]
        on_write["on_udp_filter_on_write\n(datagram sending)"]
        on_destroy["on_udp_filter_destroy"]
    end

    UFC --> UF --> Hooks
```

---

## Access Logger Extension

```mermaid
flowchart TD
    subgraph AccessLog["Access Logger Dynamic Module"]
        ALC["access_log_config.h"]
        AL["access_log.h"]
        ALFactory["config.h"]
    end

    subgraph Hooks["Access Log Event Hooks"]
        on_new["on_access_log_new"]
        on_log["on_access_log\n(stream complete, log entry)"]
        on_destroy["on_access_log_destroy"]
    end

    ALC --> AL --> Hooks
```

---

## Bootstrap Extension

```mermaid
flowchart TD
    subgraph Bootstrap["Bootstrap Dynamic Module"]
        BC["extension_config.h"]
        BE["extension.h"]
        BFactory["factory.h"]
    end

    subgraph Hooks["Bootstrap Event Hooks"]
        on_init["on_bootstrap_init\n(server starting)"]
        on_destroy["on_bootstrap_destroy"]
    end

    BC --> BE --> Hooks
```

---

## Load Balancer Extension

```mermaid
flowchart TD
    subgraph LB["Load Balancer Dynamic Module"]
        LBC["config.h\nlb_config.h"]
        LBImpl["load_balancer.h"]
    end

    subgraph Hooks["Load Balancer Event Hooks"]
        on_new["on_load_balancer_new"]
        on_choose["on_load_balancer_choose_host"]
        on_destroy["on_load_balancer_destroy"]
    end

    LBC --> LBImpl --> Hooks
```

---

## Cert Validator Extension

```mermaid
flowchart TD
    subgraph CertVal["Cert Validator Dynamic Module"]
        CVC["config.h"]
    end

    subgraph Hooks["Cert Validator Event Hooks"]
        on_init["on_cert_validator_init"]
        on_validate["on_cert_validator_validate\n(TLS handshake)"]
        on_destroy["on_cert_validator_destroy"]
    end

    CVC --> Hooks
```

---

## Extension Type Summary

```mermaid
flowchart TD
    DM["DynamicModule\n(shared .so loader)"]

    DM --> HTTP["HTTP Filter\n- Full L7 request/response\n- Headers, body, trailers\n- Async callouts\n- Most feature-rich"]

    DM --> Network["Network Filter\n- L4 byte streams\n- Read/write data events\n- Connection events"]

    DM --> Listener["Listener Filter\n- Connection accept time\n- Peek at initial bytes\n- Protocol detection"]

    DM --> UDP["UDP Filter\n- Datagram processing\n- Read/write datagrams"]

    DM --> AccessLog["Access Logger\n- Post-stream logging\n- Stream info access"]

    DM --> Bootstrap["Bootstrap Extension\n- Server init time\n- Early setup hooks"]

    DM --> LB["Load Balancer\n- Host selection logic\n- Custom LB algorithms"]

    DM --> CertVal["Cert Validator\n- TLS cert validation\n- Custom trust logic"]

    style HTTP fill:#e8f5e9,stroke:#2e7d32
```

---

## Proto Configuration

### DynamicModuleConfig (shared across extensions)

```mermaid
mindmap
  root((DynamicModuleConfig))
    name
      Module name
      Resolves to lib{name}.so
    do_not_close
      Skip dlclose
      Required for Go modules
    load_globally
      RTLD_GLOBAL flag
      Share symbols between modules
    metrics_namespace
      Prefix for custom metrics
      Default: dynamicmodulescustom
```

### DynamicModuleFilter (HTTP filter specific)

```mermaid
mindmap
  root((DynamicModuleFilter))
    dynamic_module_config
      Name, flags, namespace
    filter_name
      Selects implementation in module
      Passed to on_http_filter_config_new
    filter_config
      Opaque config blob
      JSON, string, or bytes
    terminal_filter
      Filter handles response directly
      No upstream forwarding
```
