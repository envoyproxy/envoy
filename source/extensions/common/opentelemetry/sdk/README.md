# OpenTelemetry SDK for Envoy

This directory contains the OpenTelemetry SDK implementation for Envoy, organized following the official OpenTelemetry C++ SDK structure and conventions.

## Overview

The `source/extensions/common/opentelemetry/sdk/` directory provides SDK functionality organized by signal type (trace, metrics, logs), following OpenTelemetry signal-based architecture and improving maintainability.

**Reference**: Inspired by [opentelemetry-cpp/sdk/src](https://github.com/open-telemetry/opentelemetry-cpp/tree/main/sdk/src)

## Directory Structure

The SDK is organized by OpenTelemetry signals and components, following official SDK conventions:

```
source/extensions/common/opentelemetry/sdk/
├── common/          # Shared types used across all signals
├── trace/           # Trace signal SDK types and constants
├── metrics/         # Metrics signal SDK types and constants
├── logs/            # Logs signal SDK types and constants
├── configuration/   # SDK configuration and initialization (planned)
├── resource/        # Resource detection and management (planned)
├── BUILD           # Main SDK build configuration
└── README.md       # This documentation
```

### Signal-Specific Implementation

#### `trace/` - Trace Signal SDK

Contains trace-specific types and constants:

- **`constants.h`** - OTLP protocol constants for trace signal
  - gRPC service method: `TRACE_SERVICE_EXPORT_METHOD`
  - HTTP endpoint: `DEFAULT_OTLP_TRACES_ENDPOINT`
  - All constants derived from official OTLP specification

- **`types.h`** - Trace signal type aliases
  - SpanKind: `OTelSpanKind` (from OpenTelemetry specification)
  - Export request/response types: `ExportRequest`, `ExportResponse` (from OTLP spec)
  - Smart pointer convenience aliases: `ExportRequestPtr`, `ExportRequestSharedPtr` (Envoy extensions)

#### `metrics/` - Metrics Signal SDK

Contains metrics-specific types and constants:

- **`constants.h`** - OTLP protocol constants for metrics signal
  - gRPC service method: `METRICS_SERVICE_EXPORT_METHOD`
  - HTTP endpoint: `DEFAULT_OTLP_METRICS_ENDPOINT`
  - All constants derived from official OTLP specification

- **`types.h`** - Metrics signal type aliases
  - Aggregation temporality: `AggregationTemporality` (from OpenTelemetry specification)
  - Export request/response types: `ExportRequest`, `ExportResponse` (from OTLP spec)
  - Smart pointer convenience aliases: `ExportRequestPtr`, `ExportRequestSharedPtr` (Envoy extensions)

#### `logs/` - Logs Signal SDK

Contains logs-specific types and constants:

- **`constants.h`** - OTLP protocol constants for logs signal
  - gRPC service method: `LOGS_SERVICE_EXPORT_METHOD`
  - HTTP endpoint: `DEFAULT_OTLP_LOGS_ENDPOINT`
  - All constants derived from official OTLP specification

- **`types.h`** - Logs signal type aliases
  - Export request/response types: `ExportRequest`, `ExportResponse` (from OTLP spec)
  - Smart pointer convenience aliases: `ExportRequestPtr`, `ExportRequestSharedPtr` (Envoy extensions)

#### `common/` - Shared SDK Components

Contains only truly shared types used across all telemetry signals:

- **`types.h`** - Common type aliases used across all signals
  - Attribute types: `OTelAttribute`, `OTelAttributes` (from OpenTelemetry C++ SDK)
  - Key-value pairs: `KeyValue` (from OTLP spec)
  - **Note**: Signal-specific types have been moved to their respective signal directories

### Future Implementation (Planned)

#### `resource/` - Resource Detection
- Resource detectors (environment, process, host)
- Resource merging and validation
- Container and cloud platform detection

#### `configuration/` - SDK Configuration
- Environment variable support (`OTEL_*` variables)
- Configuration validation and parsing
- Provider initialization

## Annotations and Origin

### Signal Organization
All code is organized by OpenTelemetry signal type:
- **Trace**: Distributed tracing functionality (`sdk/trace/`)
- **Metrics**: Application and infrastructure metrics (`sdk/metrics/`)
- **Logs**: Structured logging with correlation (`sdk/logs/`)
- **Common**: Truly shared types and utilities (`sdk/common/`)

### Origin Classification
Code is annotated by origin:
- **OpenTelemetry C++ SDK**: Derived from official SDK implementation
- **OTLP Specification**: Derived from OpenTelemetry Protocol specification
- **Envoy Extensions**: Envoy-specific convenience and integration code

### Reference Links
- [OpenTelemetry Specification](https://github.com/open-telemetry/opentelemetry-specification)
- [OpenTelemetry C++ SDK](https://github.com/open-telemetry/opentelemetry-cpp)
- [OTLP Protocol](https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/protocol/otlp.md)

## Usage

### Including SDK Components

```cpp
// For shared common types
#include "source/extensions/common/opentelemetry/sdk/common/types.h"

// For trace signal components
#include "source/extensions/common/opentelemetry/sdk/trace/constants.h"
#include "source/extensions/common/opentelemetry/sdk/trace/types.h"

// For metrics signal components
#include "source/extensions/common/opentelemetry/sdk/metrics/constants.h"
#include "source/extensions/common/opentelemetry/sdk/metrics/types.h"

// For logs signal components
#include "source/extensions/common/opentelemetry/sdk/logs/constants.h"
#include "source/extensions/common/opentelemetry/sdk/logs/types.h"
```

### BUILD Dependencies

```bazel
deps = [
    # For complete SDK functionality
    "//source/extensions/common/opentelemetry/sdk:opentelemetry_sdk_lib",
    # Or for specific signal components:
    "//source/extensions/common/opentelemetry/sdk/trace:sdk_trace_lib",
    "//source/extensions/common/opentelemetry/sdk/metrics:sdk_metrics_lib",
    "//source/extensions/common/opentelemetry/sdk/logs:sdk_logs_lib",
    "//source/extensions/common/opentelemetry/sdk/common:sdk_common_lib",
]
```

### Namespace Usage

```cpp
// For shared types
using namespace Envoy::Extensions::Common::OpenTelemetry::Sdk::Common;

// For trace signal
using namespace Envoy::Extensions::Common::OpenTelemetry::Sdk::Trace;
auto trace_method = Constants::TRACE_SERVICE_EXPORT_METHOD;
ExportRequestPtr request = std::make_unique<ExportRequest>();

// For metrics signal
using namespace Envoy::Extensions::Common::OpenTelemetry::Sdk::Metrics;
auto metrics_method = Constants::METRICS_SERVICE_EXPORT_METHOD;
ExportRequestPtr metrics_request = std::make_unique<ExportRequest>();

// For logs signal
using namespace Envoy::Extensions::Common::OpenTelemetry::Sdk::Logs;
auto logs_method = Constants::LOGS_SERVICE_EXPORT_METHOD;
ExportRequestPtr logs_request = std::make_unique<ExportRequest>();

// Shared attributes
OTelAttributes attributes;
```

## Relationship to Exporters

OTLP exporter functionality is kept separate in `source/extensions/common/opentelemetry/exporters/otlp/`:

- **SDK (this directory)**: Core OpenTelemetry types, constants, and SDK functionality organized by signal
- **Exporters**: Protocol-specific export implementations (gRPC, HTTP) that depend on SDK signal-specific libraries

This separation follows OpenTelemetry architecture where SDK provides core functionality and exporters provide protocol-specific implementations.

## Limited Scope

This implementation focuses on essential SDK components used by existing Envoy OpenTelemetry integrations. The signal-based organization provides a clean foundation for future SDK expansions while maintaining compatibility with existing code.
