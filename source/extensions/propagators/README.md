# Propagators

This directory contains trace propagation implementations for various protocols.

## Structure

The directory is organized by protocol, with variants in subdirectories when multiple exist:

```
propagators/
├── b3/                    # B3 trace propagation
│   ├── multi/             # Multi-header B3 format
│   └── single/            # Single-header B3 format
├── w3c/                   # W3C trace propagation
│   ├── tracecontext/      # W3C TraceContext specification
│   └── baggage/           # W3C Baggage specification
├── skywalking/            # SkyWalking trace propagation (single variant)
└── xray/                  # AWS X-Ray trace propagation (single variant)
```

## File Naming Convention

All propagator files follow a standardized naming convention for clarity and maintainability:

- **Header files**: `<protocol>_<variant>_propagator.h`
- **Implementation files**: `<protocol>_<variant>_propagator.cc`
- **Test files**: `<protocol>_<variant>_propagator_test.cc`

### Examples
- `b3_multi_propagator.h/.cc` - B3 multi-header format
- `b3_single_propagator.h/.cc` - B3 single-header format
- `tracecontext_propagator.h/.cc` - W3C TraceContext specification
- `baggage_propagator.h/.cc` - W3C Baggage specification
- `skywalking_propagator.h/.cc` - SkyWalking trace propagation
- `xray_propagator.h/.cc` - AWS X-Ray trace propagation

## Usage

Each propagator defines constants for header names and formats used by their respective tracing systems:

- **B3**: Zipkin's B3 propagation format (both multi-header and single-header variants)
- **W3C**: W3C Trace Context and Baggage specifications
- **SkyWalking**: Apache SkyWalking trace propagation
- **X-Ray**: AWS X-Ray trace propagation

## Build

Each variant has its own BUILD file defining the library target with the standardized naming:
- Primary library target: `<protocol>_<variant>_propagator_lib`

### Build Target Examples
- `b3_multi_propagator_lib` - B3 multi-header format library
- `b3_single_propagator_lib` - B3 single-header format library
- `tracecontext_propagator_lib` - W3C TraceContext library
- `baggage_propagator_lib` - W3C Baggage library
- `skywalking_propagator_lib` - SkyWalking library
- `xray_propagator_lib` - AWS X-Ray library
