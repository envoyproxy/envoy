# Part 6: `source/common/router/` — Routing Engine and Configuration

## Overview

The `router/` folder contains two major subsystems: (1) the **route configuration** engine that matches requests to routes, virtual hosts, and clusters, and (2) the **Router filter** that executes the upstream request. This document focuses on the route configuration side.

## Route Configuration Architecture

```mermaid
graph TD
    subgraph "Route Resolution"
        RH["Request Headers\n(:authority, :path, :method)"]
        RC["Router::Config\n(ConfigImpl)"]
        RM["RouteMatcher"]
        VH["VirtualHostImpl"]
        RE["RouteEntryImplBase"]
    end
    
    RH --> RC
    RC --> RM
    RM -->|"match authority"| VH
    VH -->|"match path/headers"| RE
    RE --> Result["RouteEntry\n(cluster, timeout, retry, etc.)"]
```

## Route Config Class Hierarchy

```mermaid
classDiagram
    class Config {
        <<interface>>
        +route(headers, stream_info, id) RouteConstSharedPtr
        +name() string
    }
    class ConfigImpl {
        -virtual_hosts_ : vector~VirtualHostImplPtr~
        -default_virtual_host_ : VirtualHostImpl
        -route_matcher_ : RouteMatcher
        +route(cb, headers, info, id)
    }
    class RouteMatcher {
        -virtual_hosts_ : map~string, VirtualHostSharedPtr~
        -wildcard_virtual_host_suffixes_ : map
        -default_virtual_host_ : VirtualHostSharedPtr
        +route(headers, info, id) RouteConstSharedPtr
    }
    class VirtualHostImpl {
        -name_ : string
        -domains_ : vector~string~
        -routes_ : vector~RouteEntryPtr~
        -virtual_cluster_matcher_
        -cors_policy_
        -rate_limit_policy_
        +getRouteFromEntries(headers, info) RouteConstSharedPtr
    }

    ConfigImpl ..|> Config
    ConfigImpl --> RouteMatcher
    RouteMatcher --> VirtualHostImpl
```

## Virtual Host Matching

```mermaid
flowchart TD
    A["Request arrives\n:authority = 'api.example.com:8080'"] --> B["RouteMatcher::route()"]
    B --> C["Strip port from authority\n→ 'api.example.com'"]
    C --> D{Exact match in\nvirtual_hosts_?}
    D -->|Yes| E["Return VirtualHostImpl"]
    D -->|No| F{Wildcard suffix match?\n*.example.com}
    F -->|Yes| G["Return wildcard VirtualHostImpl"]
    F -->|No| H{Default virtual host?}
    H -->|Yes| I["Return default_virtual_host_"]
    H -->|No| J["No route (404)"]
```

## Route Entry Types

```mermaid
classDiagram
    class RouteEntryImplBase {
        -cluster_name_ : string
        -timeout_ : Duration
        -retry_policy_ : RetryPolicyImpl
        -rate_limit_policy_ : RateLimitPolicyImpl
        -shadow_policies_ : vector
        -hedge_policy_ : HedgePolicyImpl
        -cors_policy_ : CorsPolicyImpl
        -header_parser_ : HeaderParser
        -metadata_ : Metadata
        +clusterName() string
        +timeout() Duration
        +retryPolicy() RetryPolicy
        +finalizeRequestHeaders(headers, info)
    }
    class PrefixRouteEntryImpl {
        -prefix_ : string
        +matches(headers, id) RouteConstSharedPtr
    }
    class PathRouteEntryImpl {
        -path_ : string
        +matches(headers, id) RouteConstSharedPtr
    }
    class RegexRouteEntryImpl {
        -regex_ : Regex::CompiledMatcher
        +matches(headers, id) RouteConstSharedPtr
    }
    class ConnectRouteEntryImpl {
        +matches(headers, id) RouteConstSharedPtr
    }
    class UriTemplateMatcherRouteEntryImpl {
        -uri_template_ : string
        +matches(headers, id) RouteConstSharedPtr
    }

    PrefixRouteEntryImpl --|> RouteEntryImplBase
    PathRouteEntryImpl --|> RouteEntryImplBase
    RegexRouteEntryImpl --|> RouteEntryImplBase
    ConnectRouteEntryImpl --|> RouteEntryImplBase
    UriTemplateMatcherRouteEntryImpl --|> RouteEntryImplBase
```

## Route Matching Flow

```mermaid
flowchart TD
    VH["VirtualHostImpl::getRouteFromEntries()"] --> Loop["For each route in routes_"]
    Loop --> Match{Route matches?}
    
    Match -->|"Check path"| PathCheck{Path specifier type?}
    PathCheck -->|prefix| PrefixMatch["path.startsWith(prefix)"]
    PathCheck -->|exact| ExactMatch["path == exact_path"]
    PathCheck -->|regex| RegexMatch["regex.match(path)"]
    PathCheck -->|connect| ConnectMatch["method == CONNECT"]
    
    PrefixMatch --> HeaderCheck
    ExactMatch --> HeaderCheck
    RegexMatch --> HeaderCheck
    ConnectMatch --> HeaderCheck
    
    HeaderCheck{Headers match?} -->|Yes| QueryCheck{Query params match?}
    HeaderCheck -->|No| Loop
    QueryCheck -->|Yes| Found["Return Route"]
    QueryCheck -->|No| Loop
    
    Loop -->|"No match"| NoRoute["Return nullptr"]
```

## Route Entry — What It Contains

```mermaid
graph TD
    RE["RouteEntryImplBase"]
    
    RE --> Cluster["clusterName()\n→ which upstream cluster"]
    RE --> Timeout["timeout()\n→ request timeout"]
    RE --> Retry["retryPolicy()\n→ retry config"]
    RE --> Shadow["shadowPolicies()\n→ traffic mirroring"]
    RE --> Hedge["hedgePolicy()\n→ hedged requests"]
    RE --> RateLimit["rateLimitPolicy()\n→ rate limit actions"]
    RE --> CORS["corsPolicy()\n→ CORS config"]
    RE --> Headers["headerParser()\n→ header add/remove"]
    RE --> Hash["hashPolicy()\n→ consistent hash"]
    RE --> Priority["priority()\n→ DEFAULT or HIGH"]
    RE --> Redirect["redirectEntry()\n→ redirect config"]
    RE --> InternalRedirect["internalRedirectPolicy()"]
    RE --> Decorator["decorator()\n→ tracing decoration"]
    RE --> MaxStream["maxStreamDuration()"]
    RE --> IdleTimeout["idleTimeout()"]
```

## Route Config Sources

```mermaid
graph TD
    subgraph "Route Config Providers"
        Static["StaticRouteConfigProviderImpl\n(from bootstrap YAML)"]
        RDS["RdsRouteConfigProviderImpl\n(from Route Discovery Service)"]
        Scoped["ScopedRdsConfigProvider\n(scoped routing)"]
    end
    
    subgraph "Config Management"
        RCPM["RouteConfigProviderManagerImpl\n(manages all providers)"]
        RCUR["RouteConfigUpdateReceiverImpl\n(merges RDS + VHDS)"]
    end
    
    Static --> RCPM
    RDS --> RCPM
    Scoped --> RCPM
    RDS --> RCUR
    RCUR --> ConfigImpl
    
    RCPM -->|"provides"| HCM["HttpConnectionManagerConfig\n→ routeConfigProvider()"]
```

### RDS (Route Discovery Service)

```mermaid
sequenceDiagram
    participant MgmtServer as Management Server
    participant RDS as RdsRouteConfigSubscription
    participant RCUR as RouteConfigUpdateReceiver
    participant Config as ConfigImpl

    MgmtServer->>RDS: RouteConfiguration (via gRPC/REST)
    RDS->>RCUR: onConfigUpdate(route_config)
    RCUR->>RCUR: Build new ConfigImpl from proto
    RCUR->>Config: Store new config
    Note over Config: Next request uses new routes
```

### Scoped Routes (SRDS)

```mermaid
graph TD
    subgraph "Scoped Routing"
        SR["ScopedRoutesConfigProvider"]
        SK["ScopeKeyBuilder\n(extracts scope from headers)"]
        SRI["ScopedRouteInfo\n(scope → route config)"]
        
        SR --> SK
        SR --> SRI
    end
    
    Request["Request\nx-scope: v2"] --> SK
    SK -->|"build scope key"| SRI
    SRI -->|"lookup route config"| ConfigImpl["ConfigImpl\n(for scope 'v2')"]
```

## Header Parser

`HeaderParser` adds/removes headers based on route configuration:

```mermaid
classDiagram
    class HeaderParser {
        -headers_to_add_ : vector~HeadersToAddEntry~
        -headers_to_remove_ : vector~LowerCaseString~
        +evaluateHeaders(headers, stream_info)
    }
    class HeadersToAddEntry {
        -header_name_ : LowerCaseString
        -value_ : string or Formatter
        -append_action_ : AppendAction
    }

    HeaderParser *-- HeadersToAddEntry
```

Headers can use `StreamInfo` substitution (e.g., `%UPSTREAM_HOST%`, `%REQ(header)%`).

## Per-Filter Config

Routes can carry per-filter configuration that overrides the global filter config:

```mermaid
graph TD
    VH["VirtualHost or Route"]
    VH --> PFC["PerFilterConfigs\n(map: filter_name → RouteSpecificFilterConfig)"]
    
    PFC --> PFC1["'envoy.filters.http.cors'\n→ CorsPolicy override"]
    PFC --> PFC2["'envoy.filters.http.ext_authz'\n→ AuthzPerRoute config"]
    PFC --> PFC3["'envoy.filters.http.lua'\n→ LuaPerRoute config"]
```

## File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `config_impl.h/cc` | `ConfigImpl`, `RouteMatcher`, `VirtualHostImpl`, Route entries | Route configuration and matching |
| `config_utility.h/cc` | `ConfigUtility`, matchers | Route config helpers |
| `header_parser.h/cc` | `HeaderParser`, `HeadersToAddEntry` | Header add/remove on routes |
| `per_filter_config.h/cc` | `PerFilterConfigs` | Per-route filter config |
| `rds_impl.h/cc` | `RdsRouteConfigSubscription`, `RdsRouteConfigProviderImpl` | Route Discovery Service |
| `route_config_update_receiver_impl.h/cc` | `RouteConfigUpdateReceiverImpl` | RDS + VHDS merge |
| `static_route_provider_impl.h/cc` | `StaticRouteConfigProviderImpl` | Static route config |
| `route_provider_manager.h/cc` | `RouteConfigProviderManagerImpl` | Route provider lifecycle |
| `scoped_rds.h/cc` | `ScopedRdsConfigProvider`, `ScopedRoutesConfigProviderManager` | Scoped routing |
| `scoped_config_impl.h/cc` | `ScopeKeyBuilderImpl`, `ScopedConfigImpl` | Scope key extraction |
| `vhds.h/cc` | `VhdsSubscription` | Virtual Host Discovery Service |
| `delegating_route_impl.h/cc` | `DelegatingRouteBase`, `DynamicRouteEntry` | Route delegation/override |
| `context_impl.h/cc` | `ContextImpl`, `RouteStatsContextImpl` | Router stats context |
| `metadatamatchcriteria_impl.h/cc` | `MetadataMatchCriteriaImpl` | Metadata match for subsets |
| `router_ratelimit.h/cc` | `RateLimitPolicyImpl`, rate limit actions | Rate limit policy from route |
| `tls_context_match_criteria_impl.h/cc` | `TlsContextMatchCriteriaImpl` | TLS context matching |

---

**Previous:** [Part 5 — Network Listeners and Filters](05-network-listeners-filters.md)  
**Next:** [Part 7 — Upstream Request and Retry](07-router-upstream-retry.md)
