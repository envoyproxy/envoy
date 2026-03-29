# LdsApiImpl

**Files:** `source/common/listener_manager/lds_api.h` / `.cc`  
**Size:** ~2 KB header, ~6.7 KB implementation  
**Namespace:** `Envoy::Server`

## Overview

`LdsApiImpl` implements the Listener Discovery Service (LDS) API. It subscribes to listener configuration from an xDS management server (or filesystem) and drives `ListenerManagerImpl::addOrUpdateListener()` and `removeListener()` on config changes.

## Class Hierarchy

```mermaid
%%{init: {"theme": "base", "themeVariables": {"primaryColor": "#e8edf2", "primaryTextColor": "#2d3748", "primaryBorderColor": "#8da0b8", "lineColor": "#8da0b8", "secondaryColor": "#edeaf4", "tertiaryColor": "#e8f0ea"}}}%%
classDiagram
    class LdsApiImpl {
        +onConfigUpdate(resources, version_info)
        +onConfigUpdate(added_resources, removed_resources, version_info)
        -listener_manager_: ListenerManager
        -subscription_: SubscriptionPtr
        -init_target_: Init::TargetImpl
    }

    class LdsApi {
        <<interface>>
        +versionInfo(): string
    }

    class SubscriptionBase {
        <<template>>
    }

    class SubscriptionCallbacks {
        <<interface>>
        +onConfigUpdate()
    }

    LdsApi <|-- LdsApiImpl
    SubscriptionBase <|-- LdsApiImpl
    SubscriptionCallbacks <|-- LdsApiImpl
```

## LDS Config Update Flow

```mermaid
%%{init: {"theme": "neutral", "themeVariables": {"actorBkg": "#e8edf2", "actorBorder": "#8da0b8", "actorTextColor": "#2d3748", "activationBkgColor": "#edeaf4", "activationBorderColor": "#9d97b8", "noteBkgColor": "#f5f0dc", "noteBorderColor": "#b8a87a", "noteTextColor": "#4a3f1a", "loopTextColor": "#2d3748", "labelBoxBkgColor": "#e8f0ea", "labelBoxBorderColor": "#7a9e82", "signalColor": "#4a5568", "signalTextColor": "#111827"}}}%%
sequenceDiagram
    participant XDS as xDS Management Server
    participant Sub as Subscription
    participant LDS as LdsApiImpl
    participant LM as ListenerManagerImpl

    XDS->>Sub: DiscoveryResponse (listeners)
    Sub->>LDS: onConfigUpdate(added, removed, version)

    loop for each added/updated listener
        LDS->>LM: addOrUpdateListener(listener_config, version_info)
        LM->>LM: warm → active flow
    end

    loop for each removed listener
        LDS->>LM: removeListener(listener_name)
        LM->>LM: drain existing connections
    end

    LDS->>LDS: update version_info_
```

## SotW vs Delta xDS

LDS supports both State-of-the-World (SotW) and Delta xDS protocols:

```mermaid
%%{init: {"theme": "base", "themeVariables": {"primaryColor": "#e8edf2", "primaryTextColor": "#2d3748", "primaryBorderColor": "#8da0b8", "lineColor": "#8da0b8", "edgeLabelBackground": "#f7f8fa"}}}%%
flowchart TD
    XDS["xDS Server"]:::server --> B{Protocol?}:::decision
    B -->|SotW| SotW["onConfigUpdate(all_resources, version)"]:::sotw
    B -->|Delta| Delta["onConfigUpdate(added, removed, version)"]:::delta

    SotW --> Diff["LDS computes diff:<br/>- New listeners: addOrUpdateListener<br/>- Missing listeners: removeListener<br/>- Changed listeners: addOrUpdateListener"]:::diff
    Delta --> Direct["LDS applies directly:<br/>- added: addOrUpdateListener<br/>- removed: removeListener"]:::direct

    classDef server fill:#e8edf2,stroke:#8da0b8,color:#2d3748,font-weight:bold
    classDef decision fill:#f5f0dc,stroke:#b8a87a,color:#4a3f1a,font-weight:bold
    classDef sotw fill:#edeaf4,stroke:#9d97b8,color:#2d2a40
    classDef delta fill:#e3edf5,stroke:#7a9eb8,color:#1e3348
    classDef diff fill:#e8f0ea,stroke:#7a9e82,color:#1e3324
    classDef direct fill:#f0eaf4,stroke:#9d82b8,color:#2d1e40
```

## Init Target Integration

LDS is an initialization target. Envoy waits for the first LDS response before marking the server as ready:

```mermaid
%%{init: {"theme": "neutral", "themeVariables": {"actorBkg": "#e8edf2", "actorBorder": "#8da0b8", "actorTextColor": "#2d3748", "activationBkgColor": "#edeaf4", "activationBorderColor": "#9d97b8", "noteBkgColor": "#e8f0ea", "noteBorderColor": "#7a9e82", "noteTextColor": "#1e3324", "loopTextColor": "#2d3748", "labelBoxBkgColor": "#e8f0ea", "labelBoxBorderColor": "#7a9e82", "signalColor": "#4a5568", "signalTextColor": "#111827"}}}%%
sequenceDiagram
    participant IM as Init::Manager
    participant LDS as LdsApiImpl
    participant XDS as xDS Server

    IM->>LDS: initialize(init_target)
    LDS->>XDS: subscribe(Listener type)
    XDS-->>LDS: first DiscoveryResponse
    LDS->>LDS: onConfigUpdate(listeners)
    LDS->>IM: init_target_.ready()
    Note over IM: Server can now accept traffic
```

## Error Handling

```mermaid
%%{init: {"theme": "base", "themeVariables": {"primaryColor": "#e8edf2", "primaryTextColor": "#2d3748", "primaryBorderColor": "#8da0b8", "lineColor": "#8da0b8", "edgeLabelBackground": "#f7f8fa"}}}%%
flowchart TD
    Update["LDS config update arrives"]:::entry --> Validate{Valid config?}:::decision
    Validate -->|Yes| Apply["Apply to ListenerManager"]:::success
    Validate -->|No| Reject["Reject update<br/>keep existing config"]:::failure
    Apply --> B{addOrUpdateListener succeeds?}:::decision
    B -->|Yes| Done["Listener warmed / updated"]:::success
    B -->|No| C["Log error<br/>listener_create_failure++ stat"]:::error
    Reject --> D["Log error<br/>NACK to xDS server"]:::error

    classDef entry fill:#e8edf2,stroke:#8da0b8,color:#2d3748,font-weight:bold
    classDef decision fill:#f5f0dc,stroke:#b8a87a,color:#4a3f1a,font-weight:bold
    classDef success fill:#e8f0ea,stroke:#7a9e82,color:#1e3324,font-weight:bold
    classDef failure fill:#f0e8e8,stroke:#b88a8a,color:#3f1e1e
    classDef error fill:#f4eaeb,stroke:#b88a90,color:#3f1e22
```

## Subscription Configuration

| Config Field | Purpose |
|-------------|---------|
| `lds_config.api_config_source` | gRPC or REST xDS server address |
| `lds_config.path` | Filesystem path for static LDS config |
| `lds_config.resource_api_version` | V3 API version |
| `lds_config.initial_fetch_timeout` | Max time to wait for first LDS response |
