# Part 45: OutlierDetector

**File:** `envoy/upstream/outlier_detection.h`  
**Namespace:** `Envoy::Upstream::Outlier`

## Summary

`Outlier::Detector` monitors per-host performance and ejects hosts that fail success-rate or latency thresholds. Uses `DetectorHostMonitor` for per-host data; `putResult` and `putResponseTime` feed the detector.

## UML Diagram

```mermaid
classDiagram
    class Detector {
        <<interface>>
        +addChangedStateCb(cb) void
        +successRateAverage(type) double
        +successRateEjectionThreshold(type) double
    }
    class DetectorHostMonitor {
        <<interface>>
        +putResult(result, code) void
        +putResponseTime(time) void
        +numEjections() uint32_t
    }
    Detector ..> DetectorHostMonitor : uses
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `addChangedStateCb(cb)` | Registers callback on eject/uneject. |
| `successRateAverage(type)` | Returns average success rate. |
| `successRateEjectionThreshold(type)` | Returns ejection threshold. |
| `putResult(result)` | Records per-host result. |
| `putResponseTime(time)` | Records response time. |
