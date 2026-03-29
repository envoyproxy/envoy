# Outlier Detection

**Files:** `source/common/upstream/outlier_detection_impl.h` / `.cc`  
**Size:** ~29 KB header, ~40 KB implementation  
**Namespace:** `Envoy::Upstream::Outlier`

## Overview

Outlier detection automatically ejects unhealthy hosts from the load balancing pool based on observed error rates. Envoy supports multiple detection algorithms: consecutive errors, success rate, and failure percentage. Ejected hosts are returned to the pool after a configurable exponential backoff.

## Class Hierarchy

```mermaid
classDiagram
    class DetectorImpl {
        +addChangedStateCb(cb)
        +successRateAverage(type): double
        +successRateEjectionThreshold(type): double
        -checkHostForUneject(host, monitor, now)
        -ejectHost(host, type)
        -host_monitors_: map~HostPtr, DetectorHostMonitorImplPtr~
        -interval_timer_: TimerPtr
    }

    class Detector {
        <<interface>>
    }

    class DetectorHostMonitorImpl {
        +numEjections(): uint32_t
        +lastEjectionTime(): optional~MonotonicTime~
        +lastUnejectionTime(): optional~MonotonicTime~
        +putResult(result, is_local_origin)
        +putHttpResponseCode(code)
        +putResponseTime(time)
        +successRate(type): double
        -consecutive_5xx_: atomic~uint32_t~
        -consecutive_gateway_failure_: atomic~uint32_t~
        -consecutive_local_origin_failure_: atomic~uint32_t~
        -success_rate_: SuccessRateMonitor
        -num_ejections_: uint32_t
    }

    class DetectorHostMonitor {
        <<interface>>
    }

    class SuccessRateAccumulator {
        +update(success)
        +getSuccessRate(): optional~double~
        -successes_: atomic~uint64_t~
        -total_: atomic~uint64_t~
    }

    Detector <|-- DetectorImpl
    DetectorHostMonitor <|-- DetectorHostMonitorImpl
    DetectorImpl *-- DetectorHostMonitorImpl
    DetectorHostMonitorImpl *-- SuccessRateAccumulator
```

## Ejection Algorithms

```mermaid
flowchart TD
    subgraph Algorithms["Detection Algorithms"]
        C5xx["Consecutive 5xx\n(consecutive_5xx >= threshold)"]
        CGW["Consecutive Gateway Failure\n(502/503/504 >= threshold)"]
        CLO["Consecutive Local Origin Failure\n(connect failures >= threshold)"]
        SR["Success Rate\n(host rate < avg - stdev * factor)"]
        FP["Failure Percentage\n(error% > threshold)"]
    end

    Host["Host receives response"] --> Record["Record result in\nDetectorHostMonitorImpl"]
    Record --> C5xx & CGW & CLO
    Timer["Periodic interval timer"] --> SR & FP
    C5xx & CGW & CLO & SR & FP --> Eject{"Any threshold\nexceeded?"}
    Eject -->|Yes| EjectHost["ejectHost(host, type)"]
    Eject -->|No| Continue["Continue normal operation"]
```

## Ejection / Unejection Lifecycle

```mermaid
stateDiagram-v2
    [*] --> InPool : host added to cluster
    InPool --> Ejected : outlier threshold exceeded
    Ejected --> InPool : backoff timer expires
    Ejected --> Ejected : still failing (backoff doubles)
    InPool --> [*] : host removed
```

### Exponential Backoff

```mermaid
flowchart LR
    E1["1st ejection\nbackoff = base_ejection_time"] --> E2["2nd ejection\nbackoff = base * 2"]
    E2 --> E3["3rd ejection\nbackoff = base * 4"]
    E3 --> EN["Nth ejection\nbackoff = min(base * 2^N, max_ejection_time)"]
```

## Consecutive Error Detection

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant DHMI as DetectorHostMonitorImpl
    participant DI as DetectorImpl

    Router->>DHMI: putHttpResponseCode(503)
    DHMI->>DHMI: consecutive_gateway_failure_++
    DHMI->>DHMI: consecutive_5xx_++

    alt consecutive_gateway_failure_ >= threshold
        DHMI->>DI: ejectHost(host, ConsecutiveGatewayFailure)
    else consecutive_5xx_ >= threshold
        DHMI->>DI: ejectHost(host, Consecutive5xx)
    end

    Router->>DHMI: putHttpResponseCode(200)
    DHMI->>DHMI: consecutive_gateway_failure_ = 0
    DHMI->>DHMI: consecutive_5xx_ = 0
```

## Success Rate Detection

Evaluated periodically (every `interval`), across all hosts:

```mermaid
sequenceDiagram
    participant Timer as Interval Timer
    participant DI as DetectorImpl
    participant DHMI1 as HostMonitor (host1)
    participant DHMI2 as HostMonitor (host2)
    participant DHMI3 as HostMonitor (host3)

    Timer->>DI: onIntervalTimer()
    DI->>DHMI1: getSuccessRate() = 95%
    DI->>DHMI2: getSuccessRate() = 40%
    DI->>DHMI3: getSuccessRate() = 92%

    DI->>DI: avg = (95+40+92)/3 = 75.7%
    DI->>DI: stdev = 25.2%
    DI->>DI: threshold = avg - stdev * factor = 75.7 - 25.2*1.9 = 27.8%

    alt host2 rate (40%) > threshold (27.8%)
        Note over DI: host2 passes (above threshold)
    else host2 rate < threshold
        DI->>DI: ejectHost(host2, SuccessRate)
    end
```

## Max Ejection Percentage

A safety valve prevents ejecting too many hosts:

```mermaid
flowchart TD
    A["ejectHost() called"] --> B{current ejected %\n> max_ejection_percent?}
    B -->|Yes| Skip["Skip ejection\n(too many hosts already ejected)"]
    B -->|No| Eject["Eject host from LB pool"]
    Eject --> Notify["Notify LB via\nchanged_state_callback_"]
```

## Configuration Reference

| Config | Default | Purpose |
|--------|---------|---------|
| `consecutive_5xx` | 5 | Consecutive 5xx responses before ejection |
| `consecutive_gateway_failure` | 5 | Consecutive 502/503/504 before ejection |
| `consecutive_local_origin_failure` | 5 | Consecutive connect failures before ejection |
| `interval` | 10s | Evaluation interval for success rate/failure percentage |
| `base_ejection_time` | 30s | Base ejection duration (multiplied by ejection count) |
| `max_ejection_percent` | 10% | Maximum % of hosts that can be ejected |
| `success_rate_minimum_hosts` | 5 | Min hosts for success rate calculation |
| `success_rate_request_volume` | 100 | Min requests for success rate calculation |
| `success_rate_stdev_factor` | 1900 (1.9x) | Stdev multiplier for success rate threshold |
| `failure_percentage_threshold` | 85 | Failure % above which host is ejected |
| `failure_percentage_minimum_hosts` | 5 | Min hosts for failure percentage calculation |
| `max_ejection_time` | 300s | Maximum ejection duration cap |
