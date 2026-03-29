# Config Dump Handler — `config_dump_handler.h` / `config_tracker_impl.h`

**Files:**
- `source/server/admin/config_dump_handler.h` — `ConfigDumpHandler`
- `source/server/admin/config_tracker_impl.h` — `ConfigTrackerImpl`

`ConfigDumpHandler` serves `/config_dump` — the most useful debugging endpoint in
Envoy. It returns a full snapshot of the currently active xDS configuration (clusters,
listeners, routes, endpoints, secrets, runtime) serialized as a proto JSON.

---

## Class Overview

```mermaid
classDiagram
    class ConfigTracker {
        <<interface envoy/server/config_tracker.h>>
        +add(key, cb) EntryOwnerPtr*
        +getCallbacksMap() CbsMap*
    }

    class ConfigTrackerImpl {
        +add(key, cb) EntryOwnerPtr
        +getCallbacksMap() CbsMap
        -map_ shared_ptr~CbsMap~
    }

    class EntryOwnerImpl {
        +~EntryOwnerImpl()   removes key from map_
        -map_ shared_ptr~CbsMap~
        -key_ string
    }

    class ConfigDumpHandler {
        +handlerConfigDump(headers, response, stream) Code
        -addAllConfigToDump(dump, mask, name_matcher, include_eds)
        -addResourceToDump(dump, mask, resource, name_matcher, include_eds)
        -addLbEndpoint(host, locality_lb_endpoint)
        -dumpEndpointConfigs(name_matcher) MessagePtr
        -config_tracker_ ConfigTracker
    }

    ConfigTracker <|-- ConfigTrackerImpl
    ConfigTrackerImpl o-- EntryOwnerImpl : RAII entries
    ConfigDumpHandler o-- ConfigTracker
```

---

## `ConfigTrackerImpl` — Registration Hub

Each subsystem registers a callback that, when invoked, returns a proto snapshot
of its current xDS state:

```cpp
EntryOwnerPtr add(const std::string& key, Cb cb);
// Cb = std::function<ProtobufTypes::MessagePtr(const Matchers::StringMatcher&)>
```

```mermaid
flowchart TD
    CDS[ClusterManagerImpl] -->|add clusters_cb| CT[ConfigTrackerImpl.map_]
    LDS[ListenerManagerImpl] -->|add listeners_cb| CT
    RDS[RdsRouteConfigProvider] -->|add routes_cb| CT
    SDS[SecretManagerImpl] -->|add secrets_cb| CT
    RTDS[Runtime::LoaderImpl] -->|add runtime_cb| CT

    CT -->|getCallbacksMap| CDH[ConfigDumpHandler]
    CDH -->|invoke each cb| Dump[envoy.admin.v3.ConfigDump proto]
```

`EntryOwnerImpl` is RAII — when a subsystem is destroyed (e.g., cluster removed),
its `EntryOwnerPtr` goes out of scope and the key is automatically removed from the
shared `CbsMap`. This prevents dangling callbacks.

---

## `/config_dump` Request Handling

### URL Parameters

| Parameter | Type | Description |
|---|---|---|
| `resource` | string | Filter to one resource type: `clusters`, `listeners`, `routes`, `secrets`, `runtime`, `endpoints` |
| `mask` | FieldMask string | Proto field mask — strip unneeded fields from response |
| `name_regex` | RE2 string | Filter resources by name |
| `include_eds` | (present) | Include endpoint config (EDS) — can be large; excluded by default |

### Dispatch Flow

```mermaid
flowchart TD
    A[GET /config_dump] --> B[parse query params]
    B --> C{resource param present?}
    C -->|Yes| D[addResourceToDump\nfetch only that resource type]
    C -->|No| E[addAllConfigToDump\nfetch all registered callbacks]

    D --> F{resource == endpoints?}
    F -->|Yes| G[dumpEndpointConfigs\nwalk ClusterManager hosts]
    F -->|No| H[ConfigTracker callback for matching key]

    E --> I[iterate getCallbacksMap]
    I --> J[invoke each cb with name_matcher]
    J --> K{include_eds?}
    K -->|Yes| L[also call dumpEndpointConfigs]
    K -->|No| M

    H --> N[apply mask if present]
    L --> N
    M --> N
    N --> O[serialize ConfigDump to JSON]
    O --> P[200 OK response]
```

### `addResourceToDump` vs `addAllConfigToDump`

- `addAllConfigToDump` — iterates all registered keys, invokes each callback, appends
  each result to `ConfigDump.configs`. On error (e.g., mask parse failure), returns
  a `(Code, message)` pair and stops.
- `addResourceToDump` — invokes only the single callback matching `resource=<key>`.
  If the key is `endpoints`, calls `dumpEndpointConfigs()` instead (endpoints are
  not registered via `ConfigTracker`).

---

## Endpoint Config Dump

Endpoints are handled specially — they are not registered in `ConfigTracker` because
they change too frequently and are computed on-demand:

```cpp
ProtobufTypes::MessagePtr dumpEndpointConfigs(
    const Matchers::StringMatcher& name_matcher) const;
```

Walks `server_.clusterManager()` to collect all host sets, then for each host:

```cpp
void addLbEndpoint(
    const Upstream::HostSharedPtr& host,
    envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) const;
```

Populates:
- `address` (IP:port)
- `health_status` (HEALTHY / UNHEALTHY / DRAINING / etc.)
- `metadata` (endpoint-level metadata)
- `load_balancing_weight`

The result is an `envoy.admin.v3.EndpointsConfigDump` proto appended to the
`ConfigDump`.

---

## Proto Field Mask (`mask` parameter)

When `mask=<FieldMask>` is present, `ConfigDumpHandler` applies
`FieldMaskUtil::TrimMessage()` to each resource proto before serialization. This
strips fields not listed in the mask, reducing response size.

Example: `GET /config_dump?resource=clusters&mask=static_clusters.cluster.name`
returns only cluster names, not the full cluster config.

---

## Registered Config Tracker Keys

| Key | Registered by | Proto type |
|---|---|---|
| `clusters` | `ClusterManagerImpl` | `envoy.admin.v3.ClustersConfigDump` |
| `listeners` | `ListenerManagerImpl` | `envoy.admin.v3.ListenersConfigDump` |
| `routes` | `RdsRouteConfigProviderManager` | `envoy.admin.v3.RoutesConfigDump` |
| `secrets` | `SecretManagerImpl` | `envoy.admin.v3.SecretsConfigDump` |
| `runtime` | `Runtime::LoaderImpl` | `envoy.admin.v3.RuntimeConfigDump` |
| `scoped_routes` | `ScopedRdsConfigProviderManager` | `envoy.admin.v3.ScopedRoutesConfigDump` |
| `endpoints` | _(special — via dumpEndpointConfigs)_ | `envoy.admin.v3.EndpointsConfigDump` |
