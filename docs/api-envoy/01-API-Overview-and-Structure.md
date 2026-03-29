# Part 1: Envoy API — Overview and Structure

## Table of Contents
1. [Introduction](#introduction)
2. [What is the Envoy API?](#what-is-the-envoy-api)
3. [Directory Layout](#directory-layout)
4. [Versioning Scheme](#versioning-scheme)
5. [Proto File Statistics](#proto-file-statistics)
6. [How the API Fits Together](#how-the-api-fits-together)
7. [Document Series Overview](#document-series-overview)

## Introduction

The `api/envoy/` directory contains the complete set of Protocol Buffer definitions that define Envoy's configuration surface, gRPC service interfaces, shared types, and data structures. These `.proto` files are the single source of truth for how Envoy is configured and how it communicates with external control planes (xDS), authorization services, rate limiters, access log sinks, and more.

This document series walks through every major API area, explaining the message types, service definitions, and their relationships.

## What is the Envoy API?

Envoy's API is defined entirely in Protocol Buffers (protobuf). It serves three purposes:

1. **Static Configuration** — The protobuf messages define the schema for Envoy's YAML/JSON configuration files. The root message is `Bootstrap` in `config/bootstrap/v3/bootstrap.proto`.

2. **Dynamic Configuration (xDS)** — gRPC service definitions allow a control plane to push configuration to Envoy at runtime. Services like LDS (Listener Discovery), CDS (Cluster Discovery), RDS (Route Discovery), and EDS (Endpoint Discovery) are defined under `service/`.

3. **Extension Points** — Each Envoy extension (HTTP filter, network filter, transport socket, load balancer, etc.) has its own configuration proto under `extensions/`. Extensions are referenced by name and use `google.protobuf.Any` typed configuration.

## Directory Layout

```
api/envoy/
├── admin/                    # Admin interface response types
│   ├── v2alpha/              #   (deprecated v2alpha)
│   └── v3/                   #   Config dumps, cluster status, server info
│
├── annotations/              # Proto annotations (unversioned)
│   ├── deprecation.proto     #   Field/enum deprecation metadata
│   └── resource.proto        #   xDS resource type annotations
│
├── api/                      # Legacy v2 API (deprecated)
│   └── v2/                   #   Discovery, core, listener, route, cluster
│
├── config/                   # Core configuration types (largest area)
│   ├── accesslog/            #   Access log filter configuration
│   ├── bootstrap/            #   Root Bootstrap message
│   ├── cluster/              #   Cluster (CDS) configuration
│   ├── common/               #   Shared config (DFP, key-value, matcher, tap)
│   ├── core/                 #   Core types: address, config source, health check
│   ├── endpoint/             #   Endpoint (EDS) configuration
│   ├── filter/               #   Legacy v2 filter configs
│   ├── grpc_credential/      #   gRPC channel credentials
│   ├── health_checker/       #   Custom health checker configs
│   ├── listener/             #   Listener (LDS) configuration
│   ├── metrics/              #   Stats and metrics sinks
│   ├── overload/             #   Overload manager
│   ├── ratelimit/            #   Rate limit configuration
│   ├── rbac/                 #   Role-based access control
│   ├── resource_monitor/     #   Resource monitors (heap, injected)
│   ├── retry/                #   Retry predicates
│   ├── route/                #   Route (RDS) configuration
│   ├── tap/                  #   Tap/trace configuration
│   ├── trace/                #   Distributed tracing
│   ├── transport_socket/     #   Transport socket configs (ALTS, raw, tap)
│   └── upstream/             #   Upstream-specific config
│
├── data/                     # Event and data types
│   ├── accesslog/            #   Access log entry structures
│   ├── cluster/              #   Outlier detection events
│   ├── core/                 #   Health check events
│   ├── dns/                  #   DNS table definitions
│   └── tap/                  #   Tap trace wrappers
│
├── extensions/               # Extension configuration (300+ protos)
│   ├── access_loggers/       #   File, gRPC, OTel, stream loggers
│   ├── bootstrap/            #   Bootstrap extensions
│   ├── clusters/             #   Cluster types (aggregate, DFP, Redis)
│   ├── compression/          #   Brotli, gzip, zstd
│   ├── filters/              #   HTTP, network, listener, UDP filters
│   ├── health_checkers/      #   Redis, Thrift health checkers
│   ├── load_balancing_policies/  # Ring hash, round robin, least request, etc.
│   ├── matching/             #   Input matchers
│   ├── rbac/                 #   RBAC principals, matchers, audit
│   ├── tracers/              #   OpenTelemetry, Fluentd tracers
│   ├── transport_sockets/    #   TLS, ALTS, QUIC, proxy protocol
│   ├── upstreams/            #   HTTP, TCP upstream configs
│   └── ...                   #   (40 extension categories total)
│
├── service/                  # gRPC xDS and auxiliary services
│   ├── accesslog/            #   Access Log Service (ALS)
│   ├── auth/                 #   External Authorization
│   ├── cluster/              #   CDS
│   ├── discovery/            #   ADS, base discovery types
│   ├── endpoint/             #   EDS
│   ├── ext_proc/             #   External Processing
│   ├── health/               #   HDS
│   ├── listener/             #   LDS
│   ├── load_stats/           #   Load reporting
│   ├── metrics/              #   Metrics streaming
│   ├── ratelimit/            #   Rate limit service
│   ├── route/                #   RDS, VHDS
│   ├── runtime/              #   RTDS
│   ├── secret/               #   SDS
│   ├── status/               #   CSDS
│   └── tap/                  #   Tap sink service
│
├── type/                     # Shared common types
│   ├── http/                 #   HTTP codec types
│   ├── matcher/              #   String, path, value matchers
│   ├── metadata/             #   Metadata key/kind
│   ├── tracing/              #   Custom tracing tags
│   └── v3/                   #   Percent, range, semantic version, token bucket
│
└── watchdog/                 # Watchdog actions
    └── v3/                   #   AbortActionConfig
```

## Versioning Scheme

Envoy uses directory-based API versioning:

| Version | Status | Description |
|---------|--------|-------------|
| **v3** | **Active** | Current stable API. All new development targets v3. |
| v2 | Deprecated | Legacy API, still present for reference. |
| v2alpha | Deprecated | Experimental v2 extensions. |
| v2alpha1 | Deprecated | Early experimental v2 features. |
| v1alpha1 | Deprecated | Very early experimental features. |

The `v3` API is the only actively maintained version. The `v2` directory under `api/envoy/api/v2/` and scattered `v2`/`v2alpha` directories under `config/` and `service/` are retained for backward compatibility but should not be used for new integrations.

Some protos are **unversioned** (e.g., `annotations/`, top-level files in `type/`). These are stable, cross-version utilities.

## Proto File Statistics

| Directory | .proto Files | Description |
|-----------|-------------|-------------|
| `config/` | 151 | Core configuration types |
| `extensions/` | 301 | Extension configurations |
| `type/` | 42 | Shared common types |
| `service/` | 40 | gRPC service definitions |
| `admin/` | 20 | Admin interface types |
| `api/` | 39 | Legacy v2 API (deprecated) |
| `data/` | 17 | Event and data types |
| `annotations/` | 2 | Proto annotations |
| `watchdog/` | 1 | Watchdog configuration |
| **Total** | **613** | |

## How the API Fits Together

The following diagram shows the relationship between the major API areas:

```
                    ┌─────────────────────────────┐
                    │     Bootstrap (config/)      │
                    │  Root configuration message  │
                    └──────────┬──────────────────┘
                               │
            ┌──────────────────┼──────────────────┐
            │                  │                  │
            ▼                  ▼                  ▼
    ┌───────────────┐  ┌──────────────┐  ┌──────────────────┐
    │  Static       │  │  Dynamic     │  │  Admin, Tracing,  │
    │  Resources    │  │  Resources   │  │  Metrics, Overload│
    │               │  │              │  │  (config/)        │
    │ - Listeners   │  │ - LDS config │  └──────────────────┘
    │ - Clusters    │  │ - CDS config │
    │ - Secrets     │  │ - ADS config │
    └───────┬───────┘  └──────┬───────┘
            │                 │
            │                 ▼
            │         ┌──────────────┐
            │         │  xDS Services │     ┌─────────────────┐
            │         │  (service/)   │────▶│  Control Plane   │
            │         │              │     │  (external)      │
            │         │ LDS,CDS,RDS  │     └─────────────────┘
            │         │ EDS,SDS,ADS  │
            │         └──────────────┘
            │
            ▼
    ┌───────────────────────────────────────────────┐
    │              Listener (config/listener/)       │
    │  ┌─────────────────────────────────────────┐  │
    │  │  Filter Chain (extensions/filters/)      │  │
    │  │  ┌─────────┐ ┌─────────┐ ┌───────────┐ │  │
    │  │  │Listener │ │Network  │ │HTTP       │ │  │
    │  │  │Filters  │ │Filters  │ │Filters    │ │  │
    │  │  └─────────┘ └─────────┘ └───────────┘ │  │
    │  └─────────────────────────────────────────┘  │
    └───────────────────────┬───────────────────────┘
                            │
                            ▼
    ┌───────────────────────────────────────────────┐
    │              Cluster (config/cluster/)         │
    │  ┌─────────────────────────────────────────┐  │
    │  │  Endpoints (config/endpoint/)            │  │
    │  │  Transport Sockets (extensions/)         │  │
    │  │  Health Checks (config/core/)            │  │
    │  │  Load Balancing (extensions/)            │  │
    │  └─────────────────────────────────────────┘  │
    └───────────────────────────────────────────────┘
```

**Key relationships:**
- `Bootstrap` references `Listener` and `Cluster` either statically or via xDS `ConfigSource`
- `Listener` contains `FilterChain` which references extension configs from `extensions/filters/`
- `Cluster` references `ClusterLoadAssignment` for endpoints, and extension configs for transport sockets and load balancing
- All extension configs use `google.protobuf.Any` typed configuration, keyed by extension name
- `service/` defines the gRPC interfaces that a control plane implements to serve xDS resources
- `type/` provides shared building blocks (matchers, percent, metadata) used across all areas

## Document Series Overview

This documentation is organized into five parts:

| Document | Topic | Contents |
|----------|-------|----------|
| **Part 1** (this document) | Overview and Structure | Directory layout, versioning, how APIs fit together |
| **Part 2** | Core Configuration APIs | Bootstrap, Cluster, Listener, Route, Endpoint, Core types |
| **Part 3** | xDS Discovery Services | All xDS services, discovery request/response, ADS, delta xDS |
| **Part 4** | Extensions API | Filters, transport sockets, access loggers, LB policies, etc. |
| **Part 5** | Common Types and Data | Shared types, data events, admin interface, watchdog, annotations |
