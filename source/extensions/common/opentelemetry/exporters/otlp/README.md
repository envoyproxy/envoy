# OpenTelemetry OTLP Exporters

This directory contains the OpenTelemetry OTLP (OpenTelemetry Protocol) exporters for Envoy. The scope is limited to OTLP exporter functionality only.

## Structure

The directory is organized by functionality:

- **Trace Exporters**: `grpc_trace_exporter.*` and `http_trace_exporter.*` - OTLP trace data exporters for gRPC and HTTP protocols
- **Protocol Utilities**: `otlp_utils.*` - Utilities for working with OTLP protocol buffers
- **User Agent**: `user_agent.*` - User-Agent header generation for OTLP requests
- **Base Interface**: `trace_exporter.h` - Base interface for trace exporters

## OTLP Constants by Signal

The exporters handle only trace signal constants currently:

### Trace Signal (OTLP-derived)
- Protocol buffer serialization utilities
- Trace data formatting for OTLP protocol

*Note: Future signals (metrics, logs) would be organized here following the same pattern.*

## SDK vs Internal Constants

- **SDK-derived**: Constants and utilities that follow the OpenTelemetry specification and are compatible with the official OpenTelemetry SDK
- **Internal**: Envoy-specific implementations and optimizations for OTLP export functionality

## Scope Limitation

This directory contains **only** the essential OTLP exporter code that was moved from `source/extensions/tracers/opentelemetry/`. The broader OpenTelemetry functionality (samplers, resource detectors, tracer implementations) remains in their original locations to keep the change minimal and focused.
