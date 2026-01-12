# Envoy 1.37.0 Release Summary

## Overview

Envoy 1.37.0 is a major feature release with significant enhancements to dynamic modules, HTTP processing, security features, and observability capabilities.

## Major Themes

### Dynamic Modules Expansion
- Added support for network, listener, UDP listener, and access logger filters
- Introduced streaming HTTP callouts and scheduler API for asynchronous operations
- Enhanced ABI for streaming body manipulation and header operations
- Added global module loading and improved module search path handling

### HTTP and Protocol Enhancements
- Container-aware CPU detection for improved resource utilization in containerized environments
- HTTP/2 performance optimizations including reduced allocations for well-known headers
- Enhanced cookie matching in route configuration
- Added vhost header customization and forward client cert matching via xDS matcher

### Filter Ecosystem Growth
- New transform filter for request/response body modification
- New MCP (Model Context Protocol) filter and router for AI/ML workloads
- Network-layer geoip filter for non-HTTP geolocation
- Postgres Inspector listener filter for PostgreSQL traffic routing

### Security and Authorization
- Proto API Scrubber filter now production-ready with comprehensive metrics
- Enhanced ext_authz with error response support and improved header handling
- Better TLS certificate validation failure messages in access logs
- On-demand certificate fetching via SDS

### Composite Filter Improvements
- Support for filter chains and named filter chains
- Improved scalability through filter chain reuse across match actions

### Observability
- New stats-based access logger
- Process-level rate limiting for access logs
- Enhanced OTLP stats sink with metric dropping support
- Added execution counters and improved tracing support across filters

### Router and Traffic Management
- Cluster-level retry policies, hash policies, and request mirroring
- Composite cluster extension for retry-aware cluster selection
- Substitution formatting for direct response bodies and descriptor values

### Other Notable Changes
- Fixed multiple memory leaks and crashes in HTTP/2, Lua, and connection handling
- Improved QUIC path migration using QUICHE logic
- Enhanced TCP proxy with upstream connect mode and early data buffering
- Added MaxMind Country database support for geoip

## Breaking Changes

- Changed default HTTP reset code from `NO_ERROR` to `INTERNAL_ERROR`
- Changed reset behavior to ignore upstream protocol errors by default
- Proto API Scrubber now returns `404 Not Found` instead of `403 Forbidden` for blocked methods
- Removed multiple runtime guards and legacy code paths

## Deprecations

- OpenTelemetry access log `common_config` field deprecated in favor of explicit `http_service`/`grpc_service` configuration

For complete details, see the full changelog in `changelogs/current.yaml`.
