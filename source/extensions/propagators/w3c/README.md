# W3C Trace Context and Baggage Propagator

This component provides a comprehensive, specification-compliant implementation of the [W3C Trace Context specification](https://www.w3.org/TR/trace-context/) and [W3C Baggage specification](https://www.w3.org/TR/baggage/) for Envoy.

## Overview

The W3C propagator provides clean separation between data structures and business logic, offering a reusable interface for extracting and injecting W3C distributed tracing headers. It is designed for full compliance with the official W3C specifications and uses official W3C terminology throughout.

## Specification Compliance

### W3C Trace Context Specification Compliance
- ✅ **Traceparent Format**: Complete support for version-traceId-parentId-traceFlags format with proper validation
- ✅ **Header Case Insensitivity**: Handles `traceparent`, `Traceparent`, `TRACEPARENT` correctly per specification
- ✅ **Future Version Compatibility**: Gracefully handles version values beyond 00 for forward compatibility
- ✅ **Zero ID Rejection**: Properly rejects all-zero trace IDs and span IDs per specification requirement
- ✅ **Field Length Validation**: Enforces exact hex string lengths (32-char trace ID, 16-char span ID)
- ✅ **Tracestate Concatenation**: Properly concatenates multiple tracestate headers with comma separator

### W3C Baggage Specification Compliance  
- ✅ **URL Encoding/Decoding**: Complete percent-encoding support for keys, values, and properties
- ✅ **Size Limits**: Enforces 8KB maximum total baggage size with configurable member limits
- ✅ **Member Properties**: Full support for baggage member metadata (semicolon-separated properties)
- ✅ **Format Validation**: Robust parsing of comma-separated members and property validation
- ✅ **Error Handling**: Graceful handling of malformed headers while preserving valid data

## Components

### Data Structures

- `TraceParent`: Represents the traceparent header value with version, trace-id, parent-id, and trace-flags
- `TraceState`: Represents the tracestate header value with vendor-specific key-value pairs
- `BaggageMember`: Represents a single baggage entry with key, value, and optional properties
- `Baggage`: Represents the complete baggage header with multiple baggage members
- `TraceContext`: Complete W3C context containing traceparent, tracestate, and baggage

### Propagator Interface

- `Propagator`: Main interface for extracting and injecting W3C trace context and baggage headers
- `TracingHelper`: Utility class for backward compatibility with existing Envoy tracers
- `BaggageHelper`: Utility class for integrating W3C baggage with Envoy's span baggage interface

## Usage

### Basic Trace Context Extraction

```cpp
#include "source/extensions/propagators/w3c/propagator.h"

// Check if W3C headers are present
if (Propagator::isPresent(trace_context)) {
  // Extract complete W3C context (traceparent, tracestate, baggage)
  auto result = Propagator::extract(trace_context);
  if (result.ok()) {
    const auto& w3c_context = result.value();
    // Use the extracted context...
  }
}
```

### Basic Injection

```cpp
// Create or obtain a W3C trace context
TraceContext w3c_context = ...;

// Inject all headers (traceparent, tracestate, baggage)
Propagator::inject(w3c_context, trace_context);
```

### W3C Baggage Operations

```cpp
// Check if baggage is present
if (Propagator::isBaggagePresent(trace_context)) {
  // Extract baggage
  auto baggage_result = Propagator::extractBaggage(trace_context);
  if (baggage_result.ok()) {
    const auto& baggage = baggage_result.value();
    
    // Get baggage values
    auto user_id = baggage.get("userId");
    if (user_id.has_value()) {
      // Use user_id.value()...
    }
  }
}

// Create and inject baggage
Baggage baggage;
baggage.set("userId", "alice");
baggage.set("sessionId", "xyz123");
Propagator::injectBaggage(baggage, trace_context);
```

### Span Baggage Interface Integration

```cpp
// Implement standard Span baggage methods using W3C propagator
class MySpan : public Tracing::Span {
public:
  std::string getBaggage(absl::string_view key) override {
    return BaggageHelper::getBaggageValue(trace_context_, key);
  }

  void setBaggage(absl::string_view key, absl::string_view value) override {
    BaggageHelper::setBaggageValue(trace_context_, key, value);
  }
};
```

### Creating New Contexts

```cpp
// Create a root trace context
auto root = Propagator::createRoot("4bf92f3577b34da6a3ce929d0e0e4736", 
                                   "00f067aa0ba902b7", 
                                   true /* sampled */);

// Create a child context
auto child = Propagator::createChild(parent_context, "b7ad6b7169203331");
```

### Backward Compatibility

```cpp
// For existing tracers that need extracted values
auto extracted = TracingHelper::extractForTracer(trace_context);
if (extracted.has_value()) {
  const auto& values = extracted.value();
  // values.version, values.trace_id, values.span_id, etc.
}
```

## W3C Baggage Features

### Specification Compliance

- URL encoding/decoding of baggage keys and values
- Support for baggage member properties (metadata)
- Size limits enforcement (8KB total, configurable member limits)
- Proper comma-separated member parsing
- Semicolon-separated property parsing

### Advanced Baggage Usage

```cpp
// Create baggage member with properties
BaggageMember member("userId", "alice");
member.setProperty("version", "2.0");
member.setProperty("sensitive", "true");

// Add to baggage
Baggage baggage;
baggage.set(member);

// Parse complex baggage strings
auto result = Baggage::parse("userId=alice;version=2.0,sessionId=xyz123;ttl=3600");
```

## Compliance

This implementation provides complete compliance with both W3C specifications:

### W3C Trace Context Compliance
- **Traceparent Validation**: Enforces exact 55-character length, version-traceId-parentId-traceFlags format
- **Header Case Handling**: Case-insensitive header processing per HTTP specification requirements
- **Zero ID Rejection**: Rejects zero trace IDs and span IDs as required by specification
- **Future Version Support**: Handles version values beyond 00 with backward compatibility
- **Hex Validation**: Validates all trace and span IDs as proper hex strings
- **Sampling Flags**: Proper handling of trace-flags field for sampling decisions

### W3C Baggage Compliance
- **URL Encoding**: Percent-encoding/decoding for all baggage keys, values, and properties per specification
- **Size Enforcement**: 8KB total size limit with proper rejection of oversized baggage
- **Member Format**: Comma-separated members with semicolon-separated properties
- **Property Support**: Full support for baggage member metadata and properties
- **Error Tolerance**: Graceful handling of invalid members while preserving valid ones
- **Character Validation**: Proper validation of allowed characters in keys and values

### Specification References
- [W3C Trace Context Specification](https://www.w3.org/TR/trace-context/)
- [W3C Baggage Specification](https://www.w3.org/TR/baggage/)
- Official W3C terminology and data structures used throughout

## Migration Benefits

For existing Envoy tracers:

- **Eliminates ~200 lines of duplicated W3C parsing code per tracer**
- **Adds W3C Baggage support to all tracers** via standard Span interface
- **Ensures specification compliance** across all Envoy tracers
- **Centralizes validation and parsing logic**
- **Provides backward compatibility** for smooth migration

## Testing

Comprehensive test coverage validates specification compliance including:

### W3C Trace Context Testing
- **Format Validation**: All traceparent format requirements and edge cases
- **Case Insensitivity**: Header processing with mixed case variations
- **Version Compatibility**: Future version handling and backward compatibility
- **Zero ID Rejection**: Validation of zero trace ID and span ID handling
- **Hex Validation**: Invalid hex string rejection and proper error handling
- **Round-trip Consistency**: Extraction followed by injection maintains data integrity

### W3C Baggage Testing  
- **Size Limits**: 8KB enforcement and proper rejection behavior
- **URL Encoding**: Complete percent-encoding/decoding test coverage
- **Member Properties**: Property parsing and preservation validation
- **Error Handling**: Malformed header handling while preserving valid data
- **Format Compliance**: Comma/semicolon parsing and format validation
- **Integration**: End-to-end testing with Envoy's trace context system

### Span Baggage Interface Testing
- **Standard API**: getBaggage()/setBaggage() method compatibility
- **Data Persistence**: Baggage preservation across span operations
- **Multi-value Support**: Multiple baggage entries and property handling
