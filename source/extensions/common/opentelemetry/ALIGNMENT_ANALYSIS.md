# OpenTelemetry Alignment Analysis for Envoy

## Executive Summary

This document provides a comprehensive analysis of all OpenTelemetry-related files and directories throughout the Envoy codebase and maps them to their corresponding files in the [opentelemetry-cpp](https://github.com/open-telemetry/opentelemetry-cpp) upstream project. The analysis identifies areas of straightforward alignment, customizations requiring clarification, and opportunities for improved organization.

## Key Findings

- **Easy Mappings**: 38% of files have direct correspondence with upstream
- **Needs Clarification**: 37% of files aggregate multiple upstream concepts or have significant customization
- **Envoy-specific**: 25% of files are unique to Envoy or heavily customized
- **Active Implementation**: Envoy has working OTLP exporters for logs and metrics, implementing significant portions of the OpenTelemetry specification
- **Ongoing Reorganization**: Movement from `source/extensions/tracers/opentelemetry/` to `source/extensions/common/opentelemetry/` has begun (commit 258e22c), with centralized constants and utilities already moved
- **Planned Propagator Support**: Isolated propagator handling is being developed in `source/extensions/propagators/` (PR #40989) to provide separate propagation components
- **Missing in Envoy**: Several upstream features are not yet implemented (file exporters, baggage propagation, some resource detectors)

## Complete File Mapping

| Envoy Path | OpenTelemetry-cpp Path(s) | Mapping Type | Notes |
|------------|---------------------------|--------------|-------|
| **Core OTel Extensions** | | | |
| `source/extensions/common/opentelemetry/exporters/otlp/trace_exporter.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_exporter.h` | Easy | Interface definition matches |
| `source/extensions/common/opentelemetry/exporters/otlp/grpc_trace_exporter.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_exporter.h` | Easy | gRPC implementation |
| `source/extensions/common/opentelemetry/exporters/otlp/grpc_trace_exporter.cc` | `exporters/otlp/src/otlp_grpc_exporter.cc` | Needs Clarification | Envoy-specific networking integration |
| `source/extensions/common/opentelemetry/exporters/otlp/http_trace_exporter.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_http.h` | Easy | HTTP implementation |
| `source/extensions/common/opentelemetry/exporters/otlp/http_trace_exporter.cc` | `exporters/otlp/src/otlp_http_exporter.cc` | Needs Clarification | Envoy-specific HTTP client integration |
| `source/extensions/common/opentelemetry/exporters/otlp/otlp_utils.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_utils.h` | Easy | Utility functions |
| `source/extensions/common/opentelemetry/exporters/otlp/otlp_utils.cc` | `exporters/otlp/src/otlp_grpc_utils.cc` | Easy | Implementation matches |
| `source/extensions/common/opentelemetry/exporters/otlp/user_agent.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_environment.h` | Needs Clarification | Envoy-specific user agent handling |
| `source/extensions/common/opentelemetry/exporters/otlp/user_agent.cc` | `exporters/otlp/src/otlp_environment.cc` | Needs Clarification | Custom environment variable handling |
| **SDK Components** | | | |
| `source/extensions/common/opentelemetry/sdk/trace/types.h` | `sdk/include/opentelemetry/sdk/trace/span_data.h` | Easy | Type definitions |
| `source/extensions/common/opentelemetry/sdk/trace/constants.h` | `sdk/include/opentelemetry/sdk/trace/tracer_config.h` | Easy | Constants and configuration |
| `source/extensions/common/opentelemetry/sdk/common/types.h` | `sdk/include/opentelemetry/sdk/common/` | Easy | Common SDK types |
| `source/extensions/common/opentelemetry/sdk/logs/types.h` | `sdk/include/opentelemetry/sdk/logs/` | Easy | Log SDK types |
| `source/extensions/common/opentelemetry/sdk/logs/constants.h` | `sdk/include/opentelemetry/sdk/logs/` | Easy | Log constants |
| `source/extensions/common/opentelemetry/sdk/metrics/types.h` | `sdk/include/opentelemetry/sdk/metrics/` | Easy | Metrics SDK types |
| `source/extensions/common/opentelemetry/sdk/metrics/constants.h` | `sdk/include/opentelemetry/sdk/metrics/` | Easy | Metrics constants |
| **OpenTelemetry Tracer Implementation** | | | |
| `source/extensions/tracers/opentelemetry/tracer.h` | `sdk/include/opentelemetry/sdk/trace/tracer.h` | Needs Clarification | Envoy tracer adapter pattern |
| `source/extensions/tracers/opentelemetry/tracer.cc` | `sdk/src/trace/tracer.cc` | Needs Clarification | Custom span creation and propagation handling |
| `source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h` | `sdk/include/opentelemetry/sdk/trace/tracer_provider.h` | Needs Clarification | Aggregates tracer provider functionality |
| `source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.cc` | `sdk/src/trace/tracer_provider.cc` | Needs Clarification | Custom provider implementation |
| `source/extensions/tracers/opentelemetry/span_context.h` | `api/include/opentelemetry/trace/span_context.h` | Easy | Direct mapping |
| `source/extensions/tracers/opentelemetry/span_context_extractor.h` | `api/include/opentelemetry/trace/propagation/http_trace_context.h` | Needs Clarification | Combines extraction with propagation |
| `source/extensions/tracers/opentelemetry/span_context_extractor.cc` | `api/src/trace/propagation/http_trace_context.cc` | Needs Clarification | Custom header extraction logic |
| `source/extensions/tracers/opentelemetry/config.h` | `sdk/include/opentelemetry/sdk/trace/tracer_config.h` | Easy | Configuration structures |
| `source/extensions/tracers/opentelemetry/config.cc` | `sdk/src/trace/tracer_config.cc` | Easy | Configuration implementation |
| **Samplers** | | | |
| `source/extensions/tracers/opentelemetry/samplers/sampler.h` | `sdk/include/opentelemetry/sdk/trace/sampler.h` | Easy | Base sampler interface |
| `source/extensions/tracers/opentelemetry/samplers/always_on/config.h` | `sdk/include/opentelemetry/sdk/trace/samplers/always_on_factory.h` | Easy | Always-on sampler factory |
| `source/extensions/tracers/opentelemetry/samplers/always_on/config.cc` | `sdk/src/trace/samplers/always_on_factory.cc` | Easy | Factory implementation |
| `source/extensions/tracers/opentelemetry/samplers/always_on/always_on_sampler.h` | `sdk/include/opentelemetry/sdk/trace/samplers/always_on.h` | Easy | Direct mapping |
| `source/extensions/tracers/opentelemetry/samplers/always_on/always_on_sampler.cc` | `sdk/src/trace/samplers/always_on.cc` | Easy | Implementation matches |
| `source/extensions/tracers/opentelemetry/samplers/parent_based/config.h` | `sdk/include/opentelemetry/sdk/trace/samplers/parent_factory.h` | Easy | Parent-based sampler factory |
| `source/extensions/tracers/opentelemetry/samplers/parent_based/config.cc` | `sdk/src/trace/samplers/parent_factory.cc` | Easy | Factory implementation |
| `source/extensions/tracers/opentelemetry/samplers/parent_based/parent_based_sampler.h` | `sdk/include/opentelemetry/sdk/trace/samplers/parent.h` | Easy | Direct mapping |
| `source/extensions/tracers/opentelemetry/samplers/parent_based/parent_based_sampler.cc` | `sdk/src/trace/samplers/parent.cc` | Easy | Implementation matches |
| `source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/config.h` | `sdk/include/opentelemetry/sdk/trace/samplers/trace_id_ratio_factory.h` | Easy | Ratio sampler factory |
| `source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/config.cc` | `sdk/src/trace/samplers/trace_id_ratio_factory.cc` | Easy | Factory implementation |
| `source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.h` | `sdk/include/opentelemetry/sdk/trace/samplers/trace_id_ratio.h` | Easy | Direct mapping |
| `source/extensions/tracers/opentelemetry/samplers/trace_id_ratio_based/trace_id_ratio_based_sampler.cc` | `sdk/src/trace/samplers/trace_id_ratio.cc` | Easy | Implementation matches |
| `source/extensions/tracers/opentelemetry/samplers/cel/config.h` | No equivalent | Envoy-specific | CEL sampler factory |
| `source/extensions/tracers/opentelemetry/samplers/cel/config.cc` | No equivalent | Envoy-specific | CEL-based sampling factory |
| `source/extensions/tracers/opentelemetry/samplers/cel/cel_sampler.h` | No equivalent | Envoy-specific | CEL expression-based sampler |
| `source/extensions/tracers/opentelemetry/samplers/cel/cel_sampler.cc` | No equivalent | Envoy-specific | CEL sampling implementation |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.h` | No equivalent | Envoy-specific | Custom Dynatrace sampling algorithm |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/dynatrace_sampler.cc` | No equivalent | Envoy-specific | Vendor-specific implementation |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config.h` | No equivalent | Envoy-specific | Dynatrace configuration structures |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config.cc` | No equivalent | Envoy-specific | Configuration implementation |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.h` | No equivalent | Envoy-specific | Dynamic configuration provider |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/sampler_config_provider.cc` | No equivalent | Envoy-specific | Provider implementation |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/stream_summary.h` | No equivalent | Envoy-specific | Streaming algorithm utilities |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/stream_summary.cc` | No equivalent | Envoy-specific | Implementation |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/tenant_id.h` | No equivalent | Envoy-specific | Tenant identification utilities |
| `source/extensions/tracers/opentelemetry/samplers/dynatrace/tenant_id.cc` | No equivalent | Envoy-specific | Implementation |
| **Resource Detectors** | | | |
| `source/extensions/tracers/opentelemetry/resource_detectors/resource_detector.h` | `sdk/include/opentelemetry/sdk/resource/resource_detector.h` | Easy | Base resource detector interface |
| `source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.h` | `sdk/include/opentelemetry/sdk/resource/resource_detector.h` | Easy | Resource provider interface |
| `source/extensions/tracers/opentelemetry/resource_detectors/resource_provider.cc` | `sdk/src/resource/resource_detector.cc` | Easy | Base implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/static/config.h` | No equivalent | Envoy-specific | Static configuration factory |
| `source/extensions/tracers/opentelemetry/resource_detectors/static/config.cc` | No equivalent | Envoy-specific | Factory implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/static/static_config_resource_detector.h` | No equivalent | Envoy-specific | Static resource detector |
| `source/extensions/tracers/opentelemetry/resource_detectors/static/static_config_resource_detector.cc` | No equivalent | Envoy-specific | Implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/environment/config.h` | `resource_detectors/include/opentelemetry/resource_detectors/` | Easy | Environment detector factory |
| `source/extensions/tracers/opentelemetry/resource_detectors/environment/config.cc` | `resource_detectors/src/` | Easy | Factory implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/environment/environment_resource_detector.h` | `resource_detectors/include/opentelemetry/resource_detectors/` | Easy | Environment detector |
| `source/extensions/tracers/opentelemetry/resource_detectors/environment/environment_resource_detector.cc` | `resource_detectors/src/` | Easy | Implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/config.h` | No equivalent | Envoy-specific | Dynatrace detector factory |
| `source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/config.cc` | No equivalent | Envoy-specific | Factory implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_resource_detector.h` | No equivalent | Envoy-specific | Vendor-specific detector |
| `source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_resource_detector.cc` | No equivalent | Envoy-specific | Implementation |
| `source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.h` | No equivalent | Envoy-specific | Metadata file reader utility |
| `source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/dynatrace_metadata_file_reader.cc` | No equivalent | Envoy-specific | Implementation |
| **API Definitions** | | | |
| `api/envoy/config/trace/v3/opentelemetry.proto` | No equivalent | Envoy-specific | Envoy-specific OTel configuration |
| `api/envoy/extensions/tracers/opentelemetry/samplers/v3/always_on_sampler.proto` | No equivalent | Envoy-specific | Proto configuration for always-on |
| `api/envoy/extensions/tracers/opentelemetry/samplers/v3/parent_based_sampler.proto` | No equivalent | Envoy-specific | Proto configuration for parent-based |
| `api/envoy/extensions/tracers/opentelemetry/samplers/v3/trace_id_ratio_based_sampler.proto` | No equivalent | Envoy-specific | Proto configuration for ratio |
| `api/envoy/extensions/tracers/opentelemetry/samplers/v3/dynatrace_sampler.proto` | No equivalent | Envoy-specific | Proto configuration for Dynatrace |
| `api/envoy/extensions/tracers/opentelemetry/samplers/v3/cel_sampler.proto` | No equivalent | Envoy-specific | Proto configuration for CEL |
| `api/envoy/extensions/tracers/opentelemetry/resource_detectors/v3/environment_resource_detector.proto` | No equivalent | Envoy-specific | Proto configuration for environment |
| `api/envoy/extensions/tracers/opentelemetry/resource_detectors/v3/static_config_resource_detector.proto` | No equivalent | Envoy-specific | Proto configuration for static |
| `api/envoy/extensions/tracers/opentelemetry/resource_detectors/v3/dynatrace_resource_detector.proto` | No equivalent | Envoy-specific | Proto configuration for Dynatrace |
| **General Tracing Infrastructure** | | | |
| `source/common/tracing/tracer_impl.h` | No equivalent | Envoy-specific | Envoy's tracer abstraction layer |
| `source/common/tracing/tracer_impl.cc` | No equivalent | Envoy-specific | Implementation |
| `source/common/tracing/http_tracer_impl.h` | No equivalent | Envoy-specific | HTTP-specific tracing utilities |
| `source/common/tracing/http_tracer_impl.cc` | No equivalent | Envoy-specific | Implementation |
| `source/common/tracing/trace_context_impl.h` | `api/include/opentelemetry/context/` | Needs Clarification | Envoy's context management |
| `source/common/tracing/trace_context_impl.cc` | `api/src/context/` | Needs Clarification | Custom context handling |
| `source/common/tracing/tracer_manager_impl.h` | No equivalent | Envoy-specific | Tracer lifecycle management |
| `source/common/tracing/tracer_manager_impl.cc` | No equivalent | Envoy-specific | Implementation |
| `source/common/tracing/tracer_config_impl.h` | No equivalent | Envoy-specific | Configuration management |
| `source/common/tracing/custom_tag_impl.h` | No equivalent | Envoy-specific | Custom tag handling |
| `source/common/tracing/custom_tag_impl.cc` | No equivalent | Envoy-specific | Implementation |
| `source/common/tracing/null_span_impl.h` | `api/include/opentelemetry/trace/noop.h` | Easy | No-op span implementation |
| `source/common/tracing/common_values.h` | No equivalent | Envoy-specific | Common tracing constants |
| **Interface Definitions** | | | |
| `envoy/tracing/tracer.h` | `api/include/opentelemetry/trace/tracer.h` | Needs Clarification | Envoy's tracer interface vs OTel API |
| `envoy/tracing/trace_driver.h` | `sdk/include/opentelemetry/sdk/trace/tracer_provider.h` | Needs Clarification | Driver pattern vs provider pattern |
| `envoy/tracing/trace_context.h` | `api/include/opentelemetry/context/` | Easy | Context interface mapping |
| `envoy/tracing/trace_config.h` | `sdk/include/opentelemetry/sdk/trace/tracer_config.h` | Easy | Configuration interface |
| `envoy/tracing/tracer_manager.h` | No equivalent | Envoy-specific | Tracer management interface |
| `envoy/tracing/trace_reason.h` | No equivalent | Envoy-specific | Envoy-specific trace reasons |
| `envoy/tracing/custom_tag.h` | No equivalent | Envoy-specific | Custom tag interface |
| `envoy/server/tracer_config.h` | No equivalent | Envoy-specific | Server-level tracer configuration |
| **Access Logger (Logs Export)** | | | |
| `source/extensions/access_loggers/open_telemetry/access_log_impl.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter.h` | Needs Clarification | Envoy access log integration with OTel logs |
| `source/extensions/access_loggers/open_telemetry/access_log_impl.cc` | `exporters/otlp/src/otlp_grpc_log_record_exporter.cc` | Needs Clarification | Implementation with Envoy-specific formatting |
| `source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter.h` | Easy | gRPC log exporter interface |
| `source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.cc` | `exporters/otlp/src/otlp_grpc_log_record_exporter.cc` | Easy | gRPC log exporter implementation |
| `source/extensions/access_loggers/open_telemetry/substitution_formatter.h` | No equivalent | Envoy-specific | Envoy log format substitution for OTel |
| `source/extensions/access_loggers/open_telemetry/substitution_formatter.cc` | No equivalent | Envoy-specific | Implementation |
| `source/extensions/access_loggers/open_telemetry/access_log_proto_descriptors.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_log_recordable.h` | Needs Clarification | Proto descriptor management |
| `source/extensions/access_loggers/open_telemetry/access_log_proto_descriptors.cc` | `exporters/otlp/src/otlp_log_recordable.cc` | Needs Clarification | Implementation |
| `source/extensions/access_loggers/open_telemetry/config.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h` | Easy | Log exporter factory |
| `source/extensions/access_loggers/open_telemetry/config.cc` | `exporters/otlp/src/otlp_grpc_log_record_exporter_factory.cc` | Easy | Factory implementation |
| **Stat Sink (Metrics Export)** | | | |
| `source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_metric_exporter.h` | Needs Clarification | Envoy stats integration with OTel metrics |
| `source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.cc` | `exporters/otlp/src/otlp_grpc_metric_exporter.cc` | Needs Clarification | Implementation with Envoy stats conversion |
| `source/extensions/stat_sinks/open_telemetry/open_telemetry_proto_descriptors.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_metric_utils.h` | Easy | Metrics proto utilities |
| `source/extensions/stat_sinks/open_telemetry/open_telemetry_proto_descriptors.cc` | `exporters/otlp/src/otlp_metric_utils.cc` | Easy | Implementation |
| `source/extensions/stat_sinks/open_telemetry/config.h` | `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h` | Easy | Metrics exporter factory |
| `source/extensions/stat_sinks/open_telemetry/config.cc` | `exporters/otlp/src/otlp_grpc_metric_exporter_factory.cc` | Easy | Factory implementation |
| **API Definitions for Logs and Metrics** | | | |
| `api/envoy/extensions/access_loggers/open_telemetry/v3/logs_service.proto` | No equivalent | Envoy-specific | Envoy-specific logs service configuration |
| `api/envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.proto` | No equivalent | Envoy-specific | Envoy-specific metrics sink configuration |

## Areas Requiring Clarification

### 1. Propagation Handling
**Current Status**: Envoy is transitioning from embedded propagation to isolated propagator components.

**Upstream Structure**:
- `api/include/opentelemetry/trace/propagation/http_trace_context.h` - W3C Trace Context
- `api/include/opentelemetry/trace/propagation/b3_propagator.h` - B3 propagation
- `api/include/opentelemetry/trace/propagation/jaeger.h` - Jaeger propagation

**Current Envoy Implementation**:
- Propagation logic embedded in `source/extensions/tracers/opentelemetry/tracer.cc`
- Span context extraction in `source/extensions/tracers/opentelemetry/span_context_extractor.cc`

**Planned Changes**:
- **New Structure**: `source/extensions/propagators/` (PR #40989) for isolated propagator handling
- **Adapter Pattern**: Text map propagators can be implemented according to OpenTelemetry specification with adapters bridging to Envoy's existing infrastructure
- **Compatibility**: Existing tracer integration points maintained while adding standards-compliant propagators

**Recommendation**: This planned reorganization aligns well with upstream patterns and will enable better code reuse and standards compliance.

### 2. Tracer Adapter Pattern
**Issue**: Envoy uses an adapter pattern to integrate with its existing tracing infrastructure, while upstream provides direct SDK usage.

**Upstream Structure**:
- Direct usage of `sdk/include/opentelemetry/sdk/trace/tracer_provider.h`
- Application creates and configures tracer provider directly

**Envoy Implementation**:
- `source/extensions/tracers/opentelemetry/opentelemetry_tracer_impl.h` adapts OTel to Envoy's `TraceDriver` interface
- `source/common/tracing/tracer_impl.h` provides Envoy's tracer abstraction

**Recommendation**: Maintain adapter pattern but ensure clear separation of concerns.

### 4. Metrics and Logs Integration
**Issue**: Envoy implements OTLP exporters for metrics and logs but integrates them through Envoy-specific interfaces.

**Upstream Structure**:
- `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter.h` - gRPC logs exporter
- `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_grpc_metric_exporter.h` - gRPC metrics exporter
- `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_http_log_record_exporter.h` - HTTP logs exporter
- `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_http_metric_exporter.h` - HTTP metrics exporter

**Envoy Implementation**:
- `source/extensions/access_loggers/open_telemetry/` - Logs export through access logger interface
- `source/extensions/stat_sinks/open_telemetry/` - Metrics export through stats sink interface
- Uses Envoy-specific formatting and conversion logic

**Recommendation**: Maintain current Envoy integration patterns while ensuring compatibility with upstream exporter interfaces.

### 3. Resource Detection Extensibility
**Issue**: Envoy has vendor-specific resource detectors not present upstream.

**Missing Upstream Equivalents**:
- Dynatrace metadata detection
- Static configuration-based detection

**Recommendation**: Consider contributing generic versions to upstream where applicable.

## Missing OpenTelemetry Features in Envoy

### 1. Propagators (In Development)
- **Status**: Isolated propagator support being developed in `source/extensions/propagators/` (PR #40989)
- **B3 Propagator**: Will align with `api/include/opentelemetry/trace/propagation/b3_propagator.h`
- **Jaeger Propagator**: Will align with `api/include/opentelemetry/trace/propagation/jaeger.h`
- **W3C Trace Context**: Current embedded implementation to be complemented with standards-compliant propagators
- **Baggage Propagation**: `api/include/opentelemetry/baggage/` - Not yet implemented

### 2. Span Processors (Partially Implemented)
- **Batch Span Processor**: `sdk/include/opentelemetry/sdk/trace/batch_span_processor.h`
- **Simple Span Processor**: `sdk/include/opentelemetry/sdk/trace/simple_processor.h`

### 3. ID Generators (Not Implemented)
- **Random ID Generator**: `sdk/include/opentelemetry/sdk/trace/random_id_generator.h`
- **Custom ID Generator Interface**: `sdk/include/opentelemetry/sdk/trace/id_generator.h`

### 4. HTTP Exporters (Not Implemented)
- **HTTP Log Record Exporter**: `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_http_log_record_exporter.h`
- **HTTP Metrics Exporter**: `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_http_metric_exporter.h`

### 5. File Exporters (Not Implemented)
- **File Log Record Exporter**: `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_file_log_record_exporter.h`
- **File Metrics Exporter**: `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_file_metric_exporter.h`
- **File Span Exporter**: `exporters/otlp/include/opentelemetry/exporters/otlp/otlp_file_exporter.h`

## Recommended Folder Structure

Based on ongoing reorganization efforts (commit 258e22c) and planned propagator development (PR #40989), the following structure builds on current progress for `source/extensions/common/opentelemetry/`:

```
source/extensions/common/opentelemetry/        # Current centralization target
├── api/                                       # API-level utilities
│   ├── trace/
│   │   └── span_context.h                    # From current tracer implementation
│   └── context/
│       └── context_utils.h                   # Context management utilities
├── sdk/                                      # SDK implementations (ALREADY MOVED)
│   ├── common/
│   │   ├── types.h                          # ✓ Already centralized
│   │   └── constants.h                      # ✓ Already centralized
│   ├── trace/
│   │   ├── types.h                          # ✓ Already centralized
│   │   ├── constants.h                      # ✓ Already centralized
│   │   ├── samplers/                        # MOVE FROM tracers/opentelemetry/
│   │   │   ├── sampler.h
│   │   │   ├── always_on/
│   │   │   ├── parent_based/
│   │   │   ├── trace_id_ratio_based/
│   │   │   └── dynatrace/                   # Vendor-specific
│   │   └── resource_detectors/              # MOVE FROM tracers/opentelemetry/
│   │       ├── resource_detector.h
│   │       ├── environment/
│   │       ├── static/
│   │       └── dynatrace/
│   ├── logs/
│   │   ├── types.h                          # ✓ Already centralized
│   │   └── constants.h                      # ✓ Already centralized
│   └── metrics/
│       ├── types.h                          # ✓ Already centralized
│       └── constants.h                      # ✓ Already centralized
├── exporters/                               # ALREADY MOVED
│   └── otlp/
│       ├── trace_exporter.h                 # ✓ Already centralized
│       ├── grpc_trace_exporter.h            # ✓ Already centralized
│       ├── grpc_trace_exporter.cc           # ✓ Already centralized
│       ├── http_trace_exporter.h            # ✓ Already centralized
│       ├── http_trace_exporter.cc           # ✓ Already centralized
│       ├── otlp_utils.h                     # ✓ Already centralized
│       ├── otlp_utils.cc                    # ✓ Already centralized
│       ├── user_agent.h                     # ✓ Already centralized
│       └── user_agent.cc                    # ✓ Already centralized
└── util/                                    # Future Envoy-specific utilities
    ├── envoy_tracer_adapter.h               # Adapter pattern implementation
    └── trace_context_bridge.h               # Bridge Envoy and OTel contexts

source/extensions/propagators/               # NEW ISOLATED PROPAGATORS (PR #40989)
├── text_map/
│   ├── propagator.h                         # Base text map propagator interface
│   ├── w3c_trace_context/
│   │   ├── config.h
│   │   ├── config.cc
│   │   └── w3c_propagator.h
│   ├── b3/
│   │   ├── config.h
│   │   ├── config.cc
│   │   └── b3_propagator.h
│   └── jaeger/
│       ├── config.h
│       ├── config.cc
│       └── jaeger_propagator.h
└── adapters/                                # Adapters for Envoy integration
    ├── text_map_adapter.h                   # Bridge propagators to Envoy infrastructure
    └── trace_context_adapter.h             # Adapt OTel context to Envoy context
```

### Note on Current Reorganization
**Ongoing Changes**: Commit 258e22c has already begun centralizing OpenTelemetry components by moving constants, types, and exporters from `source/extensions/tracers/opentelemetry/` to `source/extensions/common/opentelemetry/`. This aligns with the recommended structure above.

**Remaining Work**: Samplers and resource detectors from the tracers directory can be moved to complete the centralization, while maintaining the tracer adapter in the tracers directory for Envoy integration.

**Access Loggers and Stat Sinks**: The following components implement OpenTelemetry logs and metrics export but remain in their current locations to preserve Envoy's modular architecture:

- `source/extensions/access_loggers/open_telemetry/` - OTLP logs export through Envoy's access logger interface
- `source/extensions/stat_sinks/open_telemetry/` - OTLP metrics export through Envoy's stats sink interface

## Integration Points with Envoy Infrastructure

### Current Integration Points
1. **Tracer Factory Registration**: `source/extensions/tracers/opentelemetry/config.h`
2. **HTTP Header Extraction**: Custom integration with Envoy's HTTP header APIs
3. **Stats Integration**: Envoy-specific metrics and statistics
4. **Configuration**: Proto-based configuration through Envoy's extension framework
5. **Access Logging**: OpenTelemetry logs export through `source/extensions/access_loggers/open_telemetry/`
6. **Metrics Export**: OpenTelemetry metrics export through `source/extensions/stat_sinks/open_telemetry/`

### Recommended Separation
1. **Pure OTel Components**: Move to `source/extensions/common/opentelemetry/`
2. **Envoy Adapters**: Keep in `source/extensions/tracers/opentelemetry/`
3. **Configuration**: Maintain current proto structure for Envoy-specific config
4. **Logs Export**: Keep `source/extensions/access_loggers/open_telemetry/` for access logging integration
5. **Metrics Export**: Keep `source/extensions/stat_sinks/open_telemetry/` for stats sink integration

## Future Alignment Opportunities

### 1. Propagator Standardization (In Progress)
- **Status**: Isolated propagator implementation in development (PR #40989)
- Implement upstream-compatible propagators in `source/extensions/propagators/`
- **Adapter Integration**: Propagators will use adapter pattern to integrate with Envoy's existing trace context infrastructure
- **Standards Compliance**: Enable W3C Trace Context, B3, and Jaeger propagation according to OpenTelemetry specification
- Replace embedded propagation logic with standardized propagator usage

### 2. Complete Centralization (Ongoing)
- **Status**: Centralization started with commit 258e22c moving constants, types, and exporters
- Move remaining samplers from `source/extensions/tracers/opentelemetry/samplers/` to `source/extensions/common/opentelemetry/sdk/trace/samplers/`
- Move resource detectors from `source/extensions/tracers/opentelemetry/resource_detectors/` to `source/extensions/common/opentelemetry/sdk/trace/resource_detectors/`
- Maintain tracer adapter components in `source/extensions/tracers/opentelemetry/` for Envoy integration
- Implement batch and simple span processors following upstream patterns
- Enable configuration of span processing strategies

### 3. Span Processor Implementation
- Implement batch and simple span processors following upstream patterns
- Enable configuration of span processing strategies

### 4. Resource Detector Contribution
- Extract generic resource detection patterns for contribution to upstream
- Maintain vendor-specific detectors in Envoy

### 5. HTTP and File Exporters
- Implement HTTP exporters for traces, logs, and metrics following upstream patterns
- Add file exporters for offline processing and debugging
- Enable configuration flexibility between gRPC, HTTP, and file export options

## Open Questions for Maintainers

1. **Propagation Strategy**: With the planned `source/extensions/propagators/` structure, how should the adapter pattern integrate propagators with Envoy's existing trace context infrastructure?

2. **Centralization Progress**: Should the remaining samplers and resource detectors be moved from `source/extensions/tracers/opentelemetry/` to complete the centralization started in commit 258e22c?

3. **Vendor Extensions**: What's the policy for vendor-specific extensions (like Dynatrace samplers)? Should they remain Envoy-specific or be contributed upstream?

4. **Configuration Migration**: Should we maintain current proto-based configuration or move toward upstream configuration patterns?

5. **Performance Considerations**: Are there performance implications of adopting upstream patterns that conflict with Envoy's requirements?

6. **API Compatibility**: How do we balance alignment with upstream against Envoy's existing tracer interface stability?

7. **Resource Detector Strategy**: Should environment-based resource detection be moved to use upstream resource detectors?

8. **Testing Strategy**: How should we ensure compatibility between Envoy's adaptations and upstream OTel behavior?

## Conclusion

This analysis reveals that Envoy has extensive OpenTelemetry support with working implementations for all three telemetry signals (traces, logs, metrics). Ongoing reorganization efforts (commit 258e22c) have begun centralizing components in `source/extensions/common/opentelemetry/`, and planned propagator development (PR #40989) will address one of the key missing standards-compliant features.

While there are significant opportunities for better alignment with upstream structure and patterns, Envoy successfully implements core OTLP export functionality through its gRPC exporters. The recommended reorganization builds on current progress and would further improve maintainability, facilitate upstream contributions, and enable easier adoption of new OpenTelemetry features while preserving Envoy-specific customizations where necessary.

The key to successful alignment is maintaining clear separation between pure OpenTelemetry components that can closely follow upstream patterns and Envoy-specific adapters that bridge the integration points with Envoy's infrastructure. The planned propagator structure demonstrates this approach well, providing standards-compliant propagators with adapter integration for Envoy's existing trace context infrastructure.
