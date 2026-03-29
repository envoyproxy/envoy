# Load Balancing Infrastructure

**Files:** `edf_scheduler.h`, `wrsq_scheduler.h`, `load_balancer_context_base.h`, `load_balancer_factory_base.h`  
**Namespace:** `Envoy::Upstream`

## Overview

The upstream directory provides the **scheduling primitives** and **base classes** used by load balancing policies. The concrete LB algorithms (round-robin, least-request, ring hash, etc.) live in `source/extensions/load_balancing_policies/`, but they build on these foundations.

## Scheduler Class Hierarchy

```mermaid
classDiagram
    class Scheduler~C~ {
        <<interface>>
        +peekAgain(cb): C*
        +pickAndAdd(cb): C*
        +add(weight, entry)
        +empty(): bool
        +clear()
    }

    class EdfScheduler~C~ {
        +peekAgain(cb): C*
        +pickAndAdd(cb): C*
        +add(weight, entry)
        +empty(): bool
        +clear()
        -queue_: PQ of EdfEntry
        -current_time_: double
    }

    class WRSQScheduler~C~ {
        +peekAgain(cb): C*
        +pickAndAdd(cb): C*
        +add(weight, entry)
        +empty(): bool
        +clear()
        -entries_: vector of WeightedEntry
        -total_weight_: double
    }

    Scheduler <|-- EdfScheduler
    Scheduler <|-- WRSQScheduler
```

## EDF Scheduler — Earliest Deadline First

Used by **weighted round-robin** and **least-request** load balancers. Provides deterministic, fair scheduling proportional to weights.

### How It Works

```mermaid
flowchart TD
    subgraph State["EDF Queue State"]
        Q["Priority Queue\n(sorted by deadline)"]
        T["current_time_ = 0"]
    end

    Add["add(weight=3, host_A)"] --> Calc["deadline = current_time + 1/weight\n= 0 + 0.333"]
    Calc --> Insert["Insert {host_A, deadline=0.333}"]

    Pick["pickAndAdd()"] --> Pop["Pop lowest deadline entry"]
    Pop --> Update["current_time_ = entry.deadline\nRe-add with new deadline = current_time + 1/weight"]
    Update --> Return["Return host_A"]
```

### EDF Scheduling Example

```mermaid
flowchart LR
    subgraph Step1["Initial State"]
        direction TB
        A1["A (weight=3, deadline=0.33)"]
        B1["B (weight=1, deadline=1.00)"]
        C1["C (weight=2, deadline=0.50)"]
    end

    subgraph Step2["After pick: A selected"]
        direction TB
        A2["A (deadline=0.67)"]
        C2["C (deadline=0.50)"]
        B2["B (deadline=1.00)"]
    end

    subgraph Step3["After pick: C selected"]
        direction TB
        A3["A (deadline=0.67)"]
        C3["C (deadline=1.00)"]
        B3["B (deadline=1.00)"]
    end

    Step1 -->|"pick()"| Step2
    Step2 -->|"pick()"| Step3
```

Over 6 picks with weights A=3, B=1, C=2: sequence is **A, C, A, B, C, A** — proportional to weights.

### `peekAgain` with Validity Check

```mermaid
sequenceDiagram
    participant LB as LoadBalancer
    participant EDF as EdfScheduler
    participant Host as Host

    LB->>EDF: peekAgain(validityCb)
    loop until valid or empty
        EDF->>EDF: peek top of queue
        EDF->>Host: validityCb(host)
        alt host is valid (healthy)
            EDF-->>LB: return host
        else host invalid (unhealthy)
            EDF->>EDF: pop and discard
        end
    end
```

## WRSQ Scheduler — Weighted Random Selection Queue

Used when **random selection proportional to weight** is needed (e.g., random LB, P2C least-request).

### How It Works

```mermaid
flowchart TD
    Add["add(weight=3, host_A)\nadd(weight=1, host_B)\nadd(weight=2, host_C)"] --> State["entries_ = [(A,3), (B,1), (C,2)]\ntotal_weight_ = 6"]

    Pick["pickAndAdd()"] --> Rand["random(0, total_weight_)\n= 4.2"]
    Rand --> Walk["Walk entries:\nA: 0-3 (skip)\nB: 3-4 (skip)\nC: 4-6 (match!)"]
    Walk --> Return["Return host_C"]
```

### Selection Probabilities

| Host | Weight | Probability |
|------|--------|-------------|
| A | 3 | 3/6 = 50% |
| B | 1 | 1/6 = 16.7% |
| C | 2 | 2/6 = 33.3% |

## LoadBalancerContextBase

Default implementation of `LoadBalancerContext` with no-op behavior:

```mermaid
classDiagram
    class LoadBalancerContext {
        <<interface>>
        +computeHashKey(): optional~HashPolicy::Hash~
        +downstreamConnection(): Network::Connection*
        +metadataMatchCriteria(): Router::MetadataMatchCriteria*
        +downstreamHeaders(): Http::RequestHeaderMap*
        +overrideHostToSelect(): OverrideHost*
        +deterministic_hash_filter(): absl::optional~uint64_t~
        +upstreamSocketOptions(): Network::Socket::OptionsSharedPtr
        +upstreamTransportSocketOptions(): TransportSocketOptionsConstSharedPtr
    }

    class LoadBalancerContextBase {
        +computeHashKey(): nullopt
        +downstreamConnection(): nullptr
        +metadataMatchCriteria(): nullptr
        +downstreamHeaders(): nullptr
        +overrideHostToSelect(): nullopt
        +deterministic_hash_filter(): nullopt
        +upstreamSocketOptions(): nullptr
        +upstreamTransportSocketOptions(): nullptr
    }

    LoadBalancerContext <|-- LoadBalancerContextBase
```

## TypedLoadBalancerFactoryBase

Base class for typed LB factories registered via proto config:

```mermaid
classDiagram
    class TypedLoadBalancerFactory {
        <<interface>>
        +create(params): LoadBalancerPtr
        +createEmptyConfigProto(): ProtobufTypes::MessagePtr
    }

    class TypedLoadBalancerFactoryBase~ConfigProto~ {
        +createEmptyConfigProto(): ConfigProto
    }

    TypedLoadBalancerFactory <|-- TypedLoadBalancerFactoryBase
```

## LB Policy Integration

```mermaid
flowchart TD
    subgraph Registry["Extension Registry"]
        RR["RoundRobin\nLoadBalancerFactory"]
        LR["LeastRequest\nLoadBalancerFactory"]
        RH["RingHash\nLoadBalancerFactory"]
        Rand["Random\nLoadBalancerFactory"]
    end

    subgraph Foundation["source/common/upstream"]
        EDF["EdfScheduler"]
        WRSQ["WRSQScheduler"]
        LBC["LoadBalancerContextBase"]
        TLBF["TypedLoadBalancerFactoryBase"]
    end

    RR -->|uses| EDF
    LR -->|uses| EDF
    Rand -->|uses| WRSQ
    RR --> TLBF
    LR --> TLBF
    RH --> TLBF
    Rand --> TLBF
```
