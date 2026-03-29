# API and Proto Tools

This document covers the tools that manage Envoy's protobuf API definitions — formatting, validation, versioning, breaking change detection, and code generation.

---

## Overview

Envoy's API surface is defined entirely in Protocol Buffer files. A rich toolchain ensures these protos remain well-formatted, backward-compatible, correctly versioned, and usable across multiple languages.

```
tools/
├── api/                                # API validation and Go codegen
│   ├── generate_go_protobuf.py         # Generate Go protobuf bindings
│   └── validate_structure.py           # Validate API directory structure
├── api_proto_breaking_change_detector/  # Breaking change detection
│   ├── detector.py                     # Core detection logic
│   ├── detector_ci.py                  # CI entrypoint
│   ├── buf_utils.py                    # Buf CLI helpers
│   └── detector_errors.py             # Error type definitions
├── api_proto_plugin/                   # Protoc plugin framework
│   ├── plugin.py                       # Main plugin logic
│   ├── annotations.py                  # Proto annotation handling
│   ├── visitor.py                      # Proto AST visitor pattern
│   ├── traverse.py                     # Proto tree traversal
│   ├── type_context.py                 # Type resolution context
│   └── plugin.bzl                      # Bazel rules
├── api_versioning/                     # API version header generation
│   └── generate_api_version_header.py
├── proto_format/                       # Proto formatting pipeline
│   ├── format_api.py                   # Format via protoxform/protoprint
│   ├── proto_format.sh                 # Entrypoint: check or fix
│   ├── proto_sync.py                   # Sync formatted protos to source
│   └── fetch_normalized_changes.py     # Fetch normalized API diffs
├── protoprint/                         # Proto pretty-printer
│   ├── protoprint.py                   # Core pretty-print logic
│   └── protoprint.bzl                  # Bazel rules
├── protojsonschema/                    # Proto → JSON Schema
│   └── generate.bzl
├── protojsonschema_with_aspects/       # Proto → JSON Schema (with aspects)
│   └── protojsonschema.bzl
├── protoshared/                        # Shared proto Bazel rules
│   └── protoshared.bzl
├── type_whisperer/                     # Type database from proto descriptors
│   ├── type_whisperer.py               # Protoc plugin: FDP → Types proto
│   ├── typedb_gen.py                   # Generate type database
│   ├── api_type_db.proto               # Type DB proto definition
│   ├── api_type_db.cc / .h            # C++ type DB implementation
│   └── various .bzl and codegen scripts
└── bootstrap2pb/                       # Bootstrap config → text proto
    └── (uses bootstrap2pb.cc from parent)
```

---

## `tools/api/` — API Structure Validation

### `validate_structure.py`

Validates that the API proto files conform to Envoy's directory layout and structural conventions. Checks include:

- Correct package naming relative to directory path
- Required files present in each API package
- Proper `BUILD` file structure for API targets

**Usage:**

```bash
bazel run //tools/api:validate_structure
```

### `generate_go_protobuf.py`

Generates Go protobuf bindings from the Envoy API protos. Used in the release pipeline to produce the Go API package that downstream Go projects consume.

---

## `tools/api_proto_breaking_change_detector/` — Breaking Change Detection

Prevents accidental backward-incompatible changes to the Envoy API by comparing the current proto definitions against a baseline.

### `detector.py`

The core logic that uses the [Buf](https://buf.build/) tool to detect breaking changes. It compares the current API state against the committed version and flags:

- Removed or renamed fields
- Changed field types or numbers
- Removed or renamed RPCs
- Removed or renamed enum values
- Package name changes

### `detector_ci.py`

The CI entrypoint that wraps `detector.py` with appropriate defaults and output formatting for continuous integration pipelines.

### `buf_utils.py`

Helper functions for invoking the Buf CLI, parsing its output, and managing temporary proto images.

### `detector_errors.py`

Defines the error types and severity levels for breaking change violations.

**Usage:**

```bash
# Run detector locally
bazel run //tools/api_proto_breaking_change_detector:detector

# Run in CI mode
bazel run //tools/api_proto_breaking_change_detector:detector_ci
```

---

## `tools/api_proto_plugin/` — Protoc Plugin Framework

A general-purpose framework for building `protoc` plugins that operate on Envoy's API protos.

### Key Components

- **`plugin.py`** — Main plugin entry point; reads `CodeGeneratorRequest` from stdin and writes `CodeGeneratorResponse` to stdout
- **`visitor.py`** — Implements the visitor pattern for traversing proto file descriptors
- **`traverse.py`** — Tree traversal logic that walks proto messages, enums, services, and fields
- **`annotations.py`** — Handles Envoy-specific proto annotations (e.g., deprecation status, migration hints)
- **`type_context.py`** — Tracks type resolution context during traversal
- **`constants.py`** — Shared constants for the plugin framework
- **`utils.py`** — Utility functions

### `plugin.bzl`

Bazel rules that make it easy to create new protoc plugins using this framework:

```starlark
api_proto_plugin(
    name = "my_plugin",
    plugin = "//tools/api_proto_plugin:plugin",
    ...
)
```

---

## `tools/api_versioning/` — API Version Header

### `generate_api_version_header.py`

Generates a C++ header file containing the current API version number. This header is consumed by the Envoy binary so it can report which API version it was built against.

**Usage:**

```bash
bazel run //tools/api_versioning:generate_api_version_header
```

---

## `tools/proto_format/` — Proto Formatting Pipeline

The proto formatting system ensures all `.proto` files follow a canonical style.

### `proto_format.sh`

The main entrypoint. Supports two modes:

```bash
# Check formatting (fail on violations)
tools/proto_format/proto_format.sh check

# Fix formatting in place
tools/proto_format/proto_format.sh fix
```

### `format_api.py`

Orchestrates the formatting pipeline:

1. Runs `protoxform` to normalize proto structure
2. Runs `protoprint` to pretty-print the output
3. Applies any Envoy-specific formatting conventions

### `proto_sync.py`

After formatting, syncs the formatted proto files back into the source tree, preserving directory structure and handling any renames.

### `fetch_normalized_changes.py`

Fetches and displays normalized diffs between the current API protos and their formatted versions — useful for reviewing what `proto_format.sh fix` would change.

---

## `tools/protoprint/` — Proto Pretty-Printer

### `protoprint.py`

A standalone proto pretty-printer that produces canonical, human-readable proto file output. It handles:

- Consistent indentation and spacing
- Field ordering
- Comment preservation and alignment
- Reserved field formatting

**Usage:**

```bash
bazel run //tools/protoprint:protoprint -- input.proto
```

---

## `tools/type_whisperer/` — Type Database Generator

Generates a database of all types used across Envoy's API protos. This database is used for type migration tracking and compatibility checking.

### Pipeline

1. **`type_whisperer.py`** — A protoc plugin that reads `FileDescriptorProto` and emits a Types proto
2. **`typedb_gen.py`** — Aggregates type whisperer output into a unified type database
3. **`api_type_db.proto` / `.cc` / `.h`** — The type database schema and C++ implementation
4. **`proto_build_targets_gen.py`** — Generates Bazel build targets for proto packages
5. **`proto_cc_source_gen.py`** — Generates C++ source from proto definitions
6. **`file_descriptor_set_text_gen.py`** — Generates text-format file descriptor sets

---

## `tools/protojsonschema/` — Proto to JSON Schema

### `generate.bzl`

Bazel rules that generate JSON Schema files from proto definitions. Useful for validating Envoy configuration in JSON format against the API schema.

## `tools/protojsonschema_with_aspects/` — Proto JSON Schema (Aspects)

### `protojsonschema.bzl`

An alternative implementation using Bazel aspects for more efficient JSON schema generation across large proto dependency graphs.

---

## `tools/bootstrap2pb/`

Converts Envoy bootstrap configuration (YAML, JSON, or binary proto) to text proto format. Uses the C++ source `bootstrap2pb.cc` in the parent directory.

**Usage:**

```bash
bazel run //tools/bootstrap2pb -- input.yaml
```

---

## Proto Tool Pipeline (How They Work Together)

```
API .proto files
       │
       ├── validate_structure.py ──→ Structural validation
       │
       ├── api_proto_breaking_change_detector ──→ Backward compatibility check
       │
       ├── proto_format.sh ──→ format_api.py ──→ protoprint.py ──→ Formatted protos
       │
       ├── api_proto_plugin framework ──→ Custom protoc plugins
       │
       ├── type_whisperer ──→ Type database
       │
       ├── protojsonschema ──→ JSON Schema
       │
       ├── generate_go_protobuf.py ──→ Go bindings
       │
       └── generate_api_version_header.py ──→ C++ version header
```

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| [02-Code-Quality-Tools.md](02-Code-Quality-Tools.md) | [01-Overview.md](01-Overview.md) | [04-Build-and-CI-Tools.md](04-Build-and-CI-Tools.md) |
