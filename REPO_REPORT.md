# Envoy Proxy Repository Report

**Generated:** 2026-08-21
**Version:** 1.40.0-dev
**Branch:** main

## Overview

Envoy is a cloud-native, high-performance edge/middle/service proxy. This report summarizes the current state of the repository.

## Repository Statistics

| Metric | Count |
|--------|-------|
| Source files (`.cc` / `.h`) | 3,140 |
| Test files (`*_test.cc`) | 1,645 |
| Extension categories | 43 |
| Contrib extensions | 28 |

## Top-Level Structure

| Directory | Purpose |
|-----------|---------|
| `source/` | Core C++ proxy implementation |
| `test/` | Unit, integration, and performance tests |
| `api/` | Protocol buffer API definitions (xDS, filters) |
| `bazel/` | Build system configuration and rules |
| `contrib/` | Community-contributed extensions |
| `mobile/` | Envoy Mobile implementation |
| `tools/` | Development tooling and scripts |
| `docs/` | Documentation source files |
| `ci/` | CI pipeline scripts |
| `configs/` | Example configuration files |
| `changelogs/` | Release changelog fragments |

## Extension Categories

The following extension categories are available under `source/extensions/`:

- **Networking:** filters (HTTP/L4), clusters, transport_sockets, upstreams, load_balancing_policies, udp_packet_writer
- **Observability:** access_loggers, stat_sinks, tracers, health_checkers, health_check
- **Security:** transport_sockets, wasm_runtime, grpc_credentials
- **Traffic Management:** router, retry, rate_limit_descriptors, queue_policy, matching, internal_redirect
- **Data Processing:** compression, formatter, content_parsers, string_matcher
- **Infrastructure:** bootstrap, config, config_subscription, listener_managers, resource_monitors, watchdog, io_socket

## Build System

- **Build tool:** Bazel (with bzlmod support)
- **Compilers:** Clang ≥ 18, GCC ≥ 13
- **C++ standard:** C++20
- **Standard library:** libc++ (Clang) / libstdc++ (GCC)

## Recent Commits

| SHA | Description |
|-----|-------------|
| `ada756c1` | Add Windows specific bazelrc (#46845) |
| `86c52279` | Queue policy extension (#43355) |
