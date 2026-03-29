# Part 10: Sequence Diagrams and Flow Charts

## Table of Contents
1. [Introduction](#introduction)
2. [Configuration Phase Sequences](#configuration-phase-sequences)
3. [Network Filter Chain Creation](#network-filter-chain-creation)
4. [HTTP Filter Chain Creation](#http-filter-chain-creation)
5. [Filter Chain Matching](#filter-chain-matching)
6. [Data Processing Flow](#data-processing-flow)
7. [Complete End-to-End Flow](#complete-end-to-end-flow)

## Introduction

This document provides sequence diagrams and flow charts showing the timing and interactions between components in Envoy's filter chain system.

## Configuration Phase Sequences

### Listener Configuration Sequence

```
┌────────────────────────────────────────────────────────────────────────────┐
│              LISTENER CONFIGURATION SEQUENCE DIAGRAM                       │
└────────────────────────────────────────────────────────────────────────────┘

User      Bootstrap    Listener      Filter         Registry      Factory      Filter
Config      Loader      Manager       Chain                                    Chain
                                     Manager                                   Builder
  │           │            │            │              │             │           │
  │ YAML/xDS  │            │            │              │             │           │
  ├──────────►│            │            │              │             │           │
  │           │            │            │              │             │           │
  │           │ Listener[] │            │              │             │           │
  │           ├───────────►│            │              │             │           │
  │           │            │            │              │             │           │
  │           │            │ For each listener:        │             │           │
  │           │            │            │              │             │           │
  │           │            │ Create     │              │             │           │
  │           │            │ Listener   │              │             │           │
  │           │            ├───────────►│              │             │           │
  │           │            │            │              │             │           │
  │           │            │            │ For each filter_chain:    │           │
  │           │            │            │              │             │           │
  │           │            │            │              │             │ Build     │
  │           │            │            │              │             │ Filter    │
  │           │            │            │              │             │ Chain     │
  │           │            │            │              │             ├──────────►│
  │           │            │            │              │             │           │
  │           │            │            │              │             │           │ For each filter:
  │           │            │            │              │             │           │
  │           │            │            │              │             │           │ Lookup
  │           │            │            │              │             │           │ Factory
  │           │            │            │              │◄────────────────────────┤
  │           │            │            │              │             │           │
  │           │            │            │              │ Factory*    │           │
  │           │            │            │              ├────────────────────────►│
  │           │            │            │              │             │           │
  │           │            │            │              │             │           │ createFilter
  │           │            │            │              │             │           │ FactoryFrom
  │           │            │            │              │             │           │ Proto()
  │           │            │            │              │             │◄──────────┤
  │           │            │            │              │             │           │
  │           │            │            │              │             │ Factory   │
  │           │            │            │              │             │ Callback  │
  │           │            │            │              │             ├──────────►│
  │           │            │            │              │             │           │
  │           │            │            │              │             │           │ Store in
  │           │            │            │              │             │           │ factories
  │           │            │            │              │             │           │ list
  │           │            │            │              │             │           │
  │           │            │            │              │             │ Filter    │
  │           │            │            │              │             │ Chain     │
  │           │            │            │◄─────────────────────────────────────┤
  │           │            │            │              │             │           │
  │           │            │ Store      │              │             │           │
  │           │            │ FilterChain│              │             │           │
  │           │            │◄───────────┤              │             │           │
  │           │            │            │              │             │           │
  │           │            │ Init       │              │             │           │
  │           │            │ Managers   │              │             │           │
  │           │            ├───────────►│              │             │           │
  │           │            │            │              │             │           │
  │           │            │◄───────────┤              │             │           │
  │           │            │            │              │             │           │
  │           │ Ready      │            │              │             │           │
  │           │◄───────────┤            │              │             │           │
  │           │            │            │              │             │           │
  │ Start     │            │            │              │             │           │
  │ Accepting │            │            │              │             │           │
  │◄──────────┤            │            │              │             │           │
  │           │            │            │              │             │           │

LEGEND:
────►  Synchronous call
┄┄┄►  Return value
```

### Filter Factory Registration (Static Initialization)

```
┌────────────────────────────────────────────────────────────────────────────┐
│            FILTER FACTORY REGISTRATION SEQUENCE                            │
└────────────────────────────────────────────────────────────────────────────┘

Program     Static        Register       Factory       Factory
Start       Init          Factory        Registry
             │             Class           │             Instance
             │              │              │               │
  ┌──────┐  │              │              │               │
  │ main │  │              │              │               │
  └───┬──┘  │              │              │               │
      │     │              │              │               │
      │     │ Before main()│              │               │
      │     │              │              │               │
      │     │ Construct    │              │               │
      │     │ static var   │              │               │
      │     ├─────────────►│              │               │
      │     │              │              │               │
      │     │              │ Constructor  │               │
      │     │              │ called       │               │
      │     │              ├──────────┐   │               │
      │     │              │          │   │               │
      │     │              │ Create   │   │               │
      │     │              │ Factory  │   │               │
      │     │              │ instance │   │               │
      │     │              ├──────────┼──────────────────►│
      │     │              │          │   │               │
      │     │              │ Register │   │               │
      │     │              │ with     │   │               │
      │     │              │ Registry │   │               │
      │     │              ├──────────┴──►│               │
      │     │              │  name,       │               │
      │     │              │  factory*    │               │
      │     │              │              │               │
      │     │              │              │ Store in      │
      │     │              │              │ global map    │
      │     │              │              ├───────────┐   │
      │     │              │              │           │   │
      │     │              │              │◄──────────┘   │
      │     │              │              │               │
      │     │              │◄─────────────┤               │
      │     │              │              │               │
      │     │◄─────────────┤              │               │
      │     │              │              │               │
      ▼     │              │              │               │
  ┌──────┐ │              │              │               │
  │ main │ │              │              │               │
  │ runs │ │              │              │               │
  └──────┘ │              │              │               │
           │              │              │               │
  Later: Runtime lookup   │              │               │
           │              │              │               │
           │              │ getFactory   │               │
           │              │ (name)       │               │
           │              ├─────────────►│               │
           │              │              │               │
           │              │              │ Lookup in map │
           │              │              ├───────────┐   │
           │              │              │           │   │
           │              │              │◄──────────┘   │
           │              │              │               │
           │              │              │ factory*      │
           │              │◄─────────────┼───────────────┤
           │              │              │               │

TIME: Static initialization → main() → Runtime
```

## Network Filter Chain Creation

### Connection Arrival and Filter Chain Setup

```
┌────────────────────────────────────────────────────────────────────────────┐
│          NETWORK FILTER CHAIN CREATION SEQUENCE                            │
└────────────────────────────────────────────────────────────────────────────┘

Client    Listener   Listener    Filter     Filter    Filter    Connection   Filter
           │         Filter      Chain      Chain     Factory      │        Instance
           │         Manager     Manager                           │
   │       │           │           │          │         │          │           │
   │ SYN   │           │           │          │         │          │           │
   ├──────►│           │           │          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │ Accept    │           │          │         │          │           │
   │       │ socket    │           │          │         │          │           │
   │       ├──────┐    │           │          │         │          │           │
   │       │      │    │           │          │         │          │           │
   │       │◄─────┘    │           │          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │ Create    │           │          │         │          │           │
   │       │ Listener  │           │          │         │          │           │
   │       │ Filter    │           │          │         │          │           │
   │       │ Manager   │           │          │         │          │           │
   │       ├──────────►│           │          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │           │ TLS       │          │         │          │           │
   │       │           │ Inspector │          │         │          │           │
   │◄──────┼───────────┼───────────┤          │         │          │           │
   │ (peek)│           │           │          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │           │ Extract   │          │         │          │           │
   │       │           │ SNI, ALPN │          │         │          │           │
   │       │           ├──────┐    │          │         │          │           │
   │       │           │      │    │          │         │          │           │
   │       │           │◄─────┘    │          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │           │ Set socket│          │         │          │           │
   │       │           │ attributes│          │         │          │           │
   │       │           ├───────────┼──────────┼─────────┼─────────►│           │
   │       │           │           │          │         │          │           │
   │       │           │◄──────────┼──────────┼─────────┼──────────┤           │
   │       │           │           │          │         │          │           │
   │       │ Find      │           │          │         │          │           │
   │       │ Filter    │           │          │         │          │           │
   │       │ Chain     │           │          │         │          │           │
   │       ├──────────────────────►│          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │           │           │ Match    │         │          │           │
   │       │           │           │ logic    │         │          │           │
   │       │           │           ├─────┐    │         │          │           │
   │       │           │           │     │    │         │          │           │
   │       │           │           │◄────┘    │         │          │           │
   │       │           │           │          │         │          │           │
   │       │           │           │ Selected │         │          │           │
   │       │           │           │ FilterChain        │          │           │
   │       │           │           ├─────────►│         │          │           │
   │       │           │           │          │         │          │           │
   │       │           │           │◄─────────┤         │          │           │
   │       │           │           │          │         │          │           │
   │       │◄──────────────────────┤          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │ Create    │           │          │         │          │           │
   │       │ Network   │           │          │         │          │           │
   │       │ Filter    │           │          │         │          │           │
   │       │ Chain     │           │          │         │          │           │
   │       ├──────┐    │           │          │         │          │           │
   │       │      │    │           │          │         │          │           │
   │       │      │ For each FilterFactoryCb: │         │          │           │
   │       │      │    │           │          │         │          │           │
   │       │      │    │           │          │ Invoke  │          │           │
   │       │      │    │           │          │ callback│          │           │
   │       │      │    │           │          ├────────►│          │           │
   │       │      │    │           │          │         │          │           │
   │       │      │    │           │          │         │ Create   │           │
   │       │      │    │           │          │         │ filter   │           │
   │       │      │    │           │          │         │ instance │           │
   │       │      │    │           │          │         ├──────────────────────►│
   │       │      │    │           │          │         │          │           │
   │       │      │    │           │          │         │          │           │ init
   │       │      │    │           │          │         │          │           │ callbacks
   │       │      │    │           │          │         │◄─────────────────────┤
   │       │      │    │           │          │         │          │           │
   │       │      │    │           │          │◄────────┤          │           │
   │       │      │    │           │          │         │          │           │
   │       │◄─────┘    │           │          │         │          │           │
   │       │           │           │          │         │          │           │
   │       │ Initialize│           │          │         │          │           │
   │       │ read      │           │          │         │          │           │
   │       │ filters   │           │          │         │          │           │
   │       ├──────────────────────────────────┼─────────┼──────────┼──────────►│
   │       │           │           │          │         │          │           │
   │       │           │           │          │         │          │           │ onNew
   │       │           │           │          │         │          │           │ Connection()
   │       │           │           │          │         │          │           ├────┐
   │       │           │           │          │         │          │           │    │
   │       │           │           │          │         │          │           │◄───┘
   │       │           │           │          │         │          │           │
   │       │◄──────────────────────────────────────────────────────────────────┤
   │       │           │           │          │         │          │           │
   │       │ Connection│           │          │         │          │           │
   │       │ ready     │           │          │         │          │           │
   │       │           │           │          │         │          │           │

PHASES:
1. Accept connection
2. Run listener filters (extract SNI)
3. Find matching filter chain
4. Instantiate network filters
5. Initialize filters
6. Connection ready for data
```

## HTTP Filter Chain Creation

### HTTP Stream Creation Sequence

```
┌────────────────────────────────────────────────────────────────────────────┐
│            HTTP FILTER CHAIN CREATION SEQUENCE                             │
└────────────────────────────────────────────────────────────────────────────┘

Client    HTTP CM    Filter      Filter      HTTP        Filter
           (Network  Chain       Factory     Filter      Instance
           Filter)   Helper      Callback    Manager
   │         │         │           │           │            │
   │ GET /   │         │           │           │            │
   ├────────►│         │           │           │            │
   │         │         │           │           │            │
   │         │ Parse   │           │           │            │
   │         │ HTTP    │           │           │            │
   │         ├────┐    │           │           │            │
   │         │    │    │           │           │            │
   │         │◄───┘    │           │           │            │
   │         │         │           │           │            │
   │         │ Create  │           │           │            │
   │         │ HTTP    │           │           │            │
   │         │ Stream  │           │           │            │
   │         ├────┐    │           │           │            │
   │         │    │    │           │           │            │
   │         │    │ Create         │           │            │
   │         │    │ Filter         │           │            │
   │         │    │ Manager        │           │            │
   │         │    ├────────────────────────────►│            │
   │         │    │    │           │           │            │
   │         │    │    │           │           │            │
   │         │◄───┘    │           │           │            │
   │         │         │           │           │            │
   │         │ Create  │           │           │            │
   │         │ Filter  │           │           │            │
   │         │ Chain   │           │           │            │
   │         ├────────►│           │           │            │
   │         │         │           │           │            │
   │         │         │ For each configured filter:         │
   │         │         │           │           │            │
   │         │         │ Get       │           │            │
   │         │         │ FilterFactory          │            │
   │         │         │ Cb        │           │            │
   │         │         ├──────┐    │           │            │
   │         │         │      │    │           │            │
   │         │         │◄─────┘    │           │            │
   │         │         │           │           │            │
   │         │         │ Invoke    │           │            │
   │         │         │ callback  │           │            │
   │         │         ├──────────►│           │            │
   │         │         │           │           │            │
   │         │         │           │ Create    │            │
   │         │         │           │ filter    │            │
   │         │         │           │ instance  │            │
   │         │         │           ├───────────────────────►│
   │         │         │           │           │            │
   │         │         │           │           │            │ init
   │         │         │           │           │            │ callbacks
   │         │         │           │◄──────────────────────┤
   │         │         │           │           │            │
   │         │         │           │ Add to    │            │
   │         │         │           │ filter    │            │
   │         │         │           │ manager   │            │
   │         │         │           ├──────────►│            │
   │         │         │           │           │            │
   │         │         │           │           │ Store      │
   │         │         │           │           │ filter     │
   │         │         │           │           ├────┐       │
   │         │         │           │           │    │       │
   │         │         │           │           │◄───┘       │
   │         │         │           │           │            │
   │         │         │           │◄──────────┤            │
   │         │         │           │           │            │
   │         │         │◄──────────┤           │            │
   │         │         │           │           │            │
   │         │◄────────┤           │           │            │
   │         │         │           │           │            │
   │         │ Decode  │           │           │            │
   │         │ headers │           │           │            │
   │         ├────────────────────────────────►│            │
   │         │         │           │           │            │
   │         │         │           │           │ Iterate    │
   │         │         │           │           │ filters    │
   │         │         │           │           ├───────────►│
   │         │         │           │           │            │
   │         │         │           │           │            │ decode
   │         │         │           │           │            │ Headers()
   │         │         │           │           │            ├────┐
   │         │         │           │           │            │    │
   │         │         │           │           │            │◄───┘
   │         │         │           │           │            │
   │         │         │           │           │◄───────────┤
   │         │         │           │           │            │
   │         │◄────────────────────────────────┤            │
   │         │         │           │           │            │

PHASES:
1. Parse HTTP request
2. Create HTTP stream
3. Create HTTP filter manager
4. Create HTTP filter chain (invoke factory callbacks)
5. Instantiate filters
6. Begin processing request through filters
```

## Filter Chain Matching

### Filter Chain Matching Algorithm Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│             FILTER CHAIN MATCHING FLOW CHART                               │
└────────────────────────────────────────────────────────────────────────────┘

                        ┌─────────────────┐
                        │ New Connection  │
                        └────────┬────────┘
                                 │
                                 ▼
                        ┌─────────────────┐
                        │ Extract:        │
                        │ - Dest IP/Port  │
                        │ - SNI           │
                        │ - ALPN          │
                        │ - Source IP     │
                        └────────┬────────┘
                                 │
                                 ▼
                    ┌────────────────────────┐
                    │ Match Destination IP?  │
                    └──┬──────────────────┬──┘
                  Yes  │                  │ No
                       ▼                  ▼
              ┌────────────────┐   ┌────────────────┐
              │ Found match    │   │ Use default    │
              └───────┬────────┘   │ filter chain   │
                      │            └────────┬───────┘
                      ▼                     │
          ┌────────────────────────┐        │
          │ Match Destination Port?│        │
          └──┬──────────────────┬──┘        │
        Yes  │                  │ No        │
             ▼                  ▼           │
    ┌────────────────┐   ┌────────────┐    │
    │ Found match    │   │ Try any    │    │
    └───────┬────────┘   │ port match │    │
            │            └─────┬──────┘    │
            ▼                  │            │
  ┌────────────────────┐       │            │
  │ Match Server Name? │       │            │
  │ (SNI)              │       │            │
  └──┬──────────────┬──┘       │            │
Yes  │              │ No       │            │
     ▼              ▼          │            │
┌────────────┐  ┌────────────┐ │            │
│ Exact      │  │ Wildcard   │ │            │
│ match      │  │ match      │ │            │
└─────┬──────┘  └─────┬──────┘ │            │
      │               │        │            │
      └───────┬───────┘        │            │
              │                │            │
              ▼                │            │
   ┌────────────────────────┐  │            │
   │ Match Transport        │  │            │
   │ Protocol?              │  │            │
   │ ("tls", "raw_buffer")  │  │            │
   └──┬────────────────┬────┘  │            │
 Yes  │                │ No    │            │
      ▼                ▼       │            │
┌──────────────┐  ┌──────────┐ │            │
│ Found match  │  │ Try any  │ │            │
└──────┬───────┘  │ protocol │ │            │
       │          └────┬─────┘ │            │
       ▼               │       │            │
┌────────────────────────┐     │            │
│ Match Application      │     │            │
│ Protocols? (ALPN)      │     │            │
└──┬────────────────┬────┘     │            │
  │ Yes            │ No        │            │
  ▼                ▼           │            │
┌──────────┐  ┌────────────┐   │            │
│ Found    │  │ Use default│   │            │
│ match    │  │ app proto  │   │            │
└────┬─────┘  └─────┬──────┘   │            │
     │              │           │            │
     └──────┬───────┘           │            │
            │                   │            │
            ▼                   │            │
  ┌────────────────────┐        │            │
  │ Match Source IP?   │        │            │
  │ (if configured)    │        │            │
  └──┬────────────┬────┘        │            │
Yes  │            │ No          │            │
     ▼            ▼             │            │
┌──────────┐ ┌────────────┐    │            │
│ Use      │ │ Use        │    │            │
│ source   │ │ matched    │    │            │
│ specific │ │ chain      │    │            │
│ chain    │ └─────┬──────┘    │            │
└────┬─────┘       │           │            │
     │             │           │            │
     └──────┬──────┘           │            │
            │                  │            │
            └──────────┬───────┘            │
                       │                    │
                       └────────┬───────────┘
                                │
                                ▼
                       ┌────────────────┐
                       │ Return         │
                       │ Selected       │
                       │ FilterChain    │
                       └────────────────┘
```

## Data Processing Flow

### Network Filter Data Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│              NETWORK FILTER DATA FLOW SEQUENCE                             │
└────────────────────────────────────────────────────────────────────────────┘

Client    Connection   Filter      Filter 1    Filter 2    Filter 3     Upstream
           │           Manager     (RBAC)      (RateLimit) (TCPProxy)
   │       │             │            │            │            │            │
   │ Data  │             │            │            │            │            │
   ├──────►│             │            │            │            │            │
   │       │             │            │            │            │            │
   │       │ onRead()    │            │            │            │            │
   │       ├────────────►│            │            │            │            │
   │       │             │            │            │            │            │
   │       │             │ Iterate read filters (FIFO):         │            │
   │       │             │            │            │            │            │
   │       │             │ onData()   │            │            │            │
   │       │             ├───────────►│            │            │            │
   │       │             │            │            │            │            │
   │       │             │            │ Check RBAC│            │            │
   │       │             │            ├────┐       │            │            │
   │       │             │            │    │       │            │            │
   │       │             │            │◄───┘       │            │            │
   │       │             │            │            │            │            │
   │       │             │            │ Continue   │            │            │
   │       │             │◄───────────┤            │            │            │
   │       │             │            │            │            │            │
   │       │             │ onData()   │            │            │            │
   │       │             ├────────────────────────►│            │            │
   │       │             │            │            │            │            │
   │       │             │            │            │ Check rate │            │
   │       │             │            │            ├────┐       │            │
   │       │             │            │            │    │       │            │
   │       │             │            │            │◄───┘       │            │
   │       │             │            │            │            │            │
   │       │             │            │            │ Continue   │            │
   │       │             │◄────────────────────────┤            │            │
   │       │             │            │            │            │            │
   │       │             │ onData()   │            │            │            │
   │       │             ├────────────────────────────────────►│            │
   │       │             │            │            │            │            │
   │       │             │            │            │            │ Forward to │
   │       │             │            │            │            │ upstream   │
   │       │             │            │            │            ├───────────►│
   │       │             │            │            │            │            │
   │       │             │            │            │            │ Stop       │
   │       │             │◄────────────────────────────────────┤ Iteration  │
   │       │             │            │            │            │            │
   │       │◄────────────┤            │            │            │            │
   │       │             │            │            │            │            │
   │       │             │            │            │            │            │
   │       │             │            │            │            │ Data from  │
   │       │             │            │            │            │ upstream   │
   │       │             │            │            │            │◄───────────┤
   │       │             │            │            │            │            │
   │       │ onWrite()   │            │            │            │            │
   │       ├────────────►│            │            │            │            │
   │       │             │            │            │            │            │
   │       │             │ Iterate write filters (LIFO):        │            │
   │       │             │            │            │            │            │
   │       │             │ onWrite()  │            │            │            │
   │       │             ├────────────────────────────────────►│            │
   │       │             │            │            │            │            │
   │       │             │            │            │            │ Continue   │
   │       │             │◄────────────────────────────────────┤            │
   │       │             │            │            │            │            │
   │       │             │ onWrite()  │            │            │            │
   │       │             ├────────────────────────►│            │            │
   │       │             │            │            │            │            │
   │       │             │            │            │ Continue   │            │
   │       │             │◄────────────────────────┤            │            │
   │       │             │            │            │            │            │
   │       │             │ onWrite()  │            │            │            │
   │       │             ├───────────►│            │            │            │
   │       │             │            │            │            │            │
   │       │             │            │ Continue   │            │            │
   │       │             │◄───────────┤            │            │            │
   │       │             │            │            │            │            │
   │       │◄────────────┤            │            │            │            │
   │       │             │            │            │            │            │
   │◄──────┤             │            │            │            │            │
   │       │             │            │            │            │            │

KEY POINTS:
- Read filters: FIFO (First In First Out) - A → B → C
- Write filters: LIFO (Last In First Out) - C → B → A
- Each filter can stop iteration
- Terminal filter (TCP Proxy) handles upstream communication
```

### HTTP Filter Data Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│               HTTP FILTER DATA FLOW SEQUENCE                               │
└────────────────────────────────────────────────────────────────────────────┘

Client   HTTP     Filter      Filter A    Filter B    Filter C     Upstream
         CM       Manager     (CORS)      (JWT)       (Router)
  │       │          │            │           │           │            │
  │ GET / │          │            │           │           │            │
  ├──────►│          │            │           │           │            │
  │       │          │            │           │           │            │
  │       │ decode   │            │           │           │            │
  │       │ Headers()│            │           │           │            │
  │       ├─────────►│            │           │           │            │
  │       │          │            │           │           │            │
  │       │          │ Iterate decoders (FIFO):          │            │
  │       │          │            │           │           │            │
  │       │          │ decode     │           │           │            │
  │       │          │ Headers()  │           │           │            │
  │       │          ├───────────►│           │           │            │
  │       │          │            │           │           │            │
  │       │          │            │ Add CORS  │           │            │
  │       │          │            │ headers   │           │            │
  │       │          │            ├────┐      │           │            │
  │       │          │            │    │      │           │            │
  │       │          │            │◄───┘      │           │            │
  │       │          │            │           │           │            │
  │       │          │            │ Continue  │           │            │
  │       │          │◄───────────┤           │           │            │
  │       │          │            │           │           │            │
  │       │          │ decode     │           │           │            │
  │       │          │ Headers()  │           │           │            │
  │       │          ├───────────────────────►│           │            │
  │       │          │            │           │           │            │
  │       │          │            │           │ Verify JWT│            │
  │       │          │            │           │ (async)   │            │
  │       │          │            │           ├────┐      │            │
  │       │          │            │           │    │      │            │
  │       │          │            │           │    │ Async│            │
  │       │          │            │           │    │ call │            │
  │       │          │            │           │    │      │            │
  │       │          │            │           │    │ Stop │            │
  │       │          │◄───────────────────────┤◄───┘ Iter │            │
  │       │          │            │           │           │            │
  │       │◄─────────┤            │           │           │            │
  │       │          │            │           │           │            │
  │       │          │  (Wait for async JWT verification)  │            │
  │       │          │            │           │           │            │
  │       │          │            │           │ JWT OK    │            │
  │       │          │            │           ├────┐      │            │
  │       │          │            │           │    │      │            │
  │       │          │            │           │◄───┘      │            │
  │       │          │            │           │           │            │
  │       │          │            │           │ continue  │            │
  │       │          │            │           │ Decoding()│            │
  │       │          │◄───────────────────────┤           │            │
  │       │          │            │           │           │            │
  │       │          │ Continue   │           │           │            │
  │       │          │ iteration  │           │           │            │
  │       │          │            │           │           │            │
  │       │          │ decode     │           │           │            │
  │       │          │ Headers()  │           │           │            │
  │       │          ├───────────────────────────────────►│            │
  │       │          │            │           │           │            │
  │       │          │            │           │           │ Match route│
  │       │          │            │           │           │ Select     │
  │       │          │            │           │           │ upstream   │
  │       │          │            │           │           ├───────────►│
  │       │          │            │           │           │            │
  │       │          │            │           │           │ Stop       │
  │       │          │◄───────────────────────────────────┤ Iteration  │
  │       │          │            │           │           │            │
  │       │◄─────────┤            │           │           │            │
  │       │          │            │           │           │            │
  │       │          │            │           │           │            │
  │       │          │            │           │           │◄───────────┤
  │       │          │            │           │           │ Response   │
  │       │ encode   │            │           │           │            │
  │       │ Headers()│            │           │           │            │
  │       ├─────────►│            │           │           │            │
  │       │          │            │           │           │            │
  │       │          │ Iterate encoders (LIFO):           │            │
  │       │          │            │           │           │            │
  │       │          │ encode     │           │           │            │
  │       │          │ Headers()  │           │           │            │
  │       │          ├───────────────────────────────────►│            │
  │       │          │            │           │           │            │
  │       │          │            │           │           │ Update     │
  │       │          │            │           │           │ headers    │
  │       │          │            │           │           ├────┐       │
  │       │          │            │           │           │    │       │
  │       │          │            │           │           │◄───┘       │
  │       │          │            │           │           │            │
  │       │          │            │           │           │ Continue   │
  │       │          │◄───────────────────────────────────┤            │
  │       │          │            │           │           │            │
  │       │          │ encode     │           │           │            │
  │       │          │ Headers()  │           │           │            │
  │       │          ├───────────────────────►│           │            │
  │       │          │            │           │           │            │
  │       │          │            │           │ No-op     │            │
  │       │          │            │           │ Continue  │            │
  │       │          │◄───────────────────────┤           │            │
  │       │          │            │           │           │            │
  │       │          │ encode     │           │           │            │
  │       │          │ Headers()  │           │           │            │
  │       │          ├───────────►│           │           │            │
  │       │          │            │           │           │            │
  │       │          │            │ Add CORS  │           │            │
  │       │          │            │ response  │           │            │
  │       │          │            │ headers   │           │            │
  │       │          │            ├────┐      │           │            │
  │       │          │            │    │      │           │            │
  │       │          │            │◄───┘      │           │            │
  │       │          │            │           │           │            │
  │       │          │            │ Continue  │           │            │
  │       │          │◄───────────┤           │           │            │
  │       │          │            │           │           │            │
  │       │◄─────────┤            │           │           │            │
  │       │          │            │           │           │            │
  │◄──────┤          │            │           │           │            │
  │       │          │            │           │           │            │

KEY POINTS:
- Decoder filters: FIFO (A → B → C)
- Encoder filters: LIFO (C → B → A)
- Filters can stop iteration for async operations
- continueDecoding() resumes iteration
- Terminal filter (Router) handles upstream request
```

## Complete End-to-End Flow

### Full Request Processing Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│              COMPLETE END-TO-END REQUEST FLOW                              │
└────────────────────────────────────────────────────────────────────────────┘

PHASE 1: CONFIGURATION (Server Startup)
═══════════════════════════════════════
┌──────────────────┐
│ Load Config      │
│ (YAML/xDS)       │
└────────┬─────────┘
         │
         ▼
┌──────────────────────────┐
│ Create Listeners         │
│ Build Filter Chains      │
│ Register Factories       │
│ Create Factory Callbacks │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Server Ready             │
│ Listening on ports       │
└──────────────────────────┘


PHASE 2: CONNECTION ESTABLISHMENT
══════════════════════════════════
         │
         │ Client connects
         ▼
┌──────────────────────────┐
│ Accept Connection        │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Listener Filters         │
│ - TLS Inspector (SNI)    │
│ - Proxy Protocol         │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Filter Chain Matching    │
│ Select correct chain     │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Create Transport Socket  │
│ (TLS handshake if needed)│
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────────────┐
│ Create Network Filter Chain      │
│ Invoke FilterFactoryCb callbacks │
│ Instantiate filter instances     │
└────────┬─────────────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Initialize Filters       │
│ onNewConnection()        │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Connection Ready         │
└──────────────────────────┘


PHASE 3: REQUEST PROCESSING
════════════════════════════
         │
         │ HTTP request arrives
         ▼
┌──────────────────────────┐
│ Network Filters          │
│ (Read path - FIFO)       │
│ - RBAC                   │
│ - Rate Limit             │
│ - HTTP Connection Mgr ───┐
└──────────────────────────┘│
                            │
    ┌───────────────────────┘
    │
    ▼
┌──────────────────────────────────┐
│ Parse HTTP Request               │
│ Create HTTP Stream               │
└────────┬─────────────────────────┘
         │
         ▼
┌──────────────────────────────────┐
│ Create HTTP Filter Chain         │
│ Invoke Http::FilterFactoryCb     │
│ Instantiate HTTP filter instances│
└────────┬─────────────────────────┘
         │
         ▼
┌──────────────────────────┐
│ HTTP Decoder Filters     │
│ (FIFO: A → B → C)        │
│ - CORS                   │
│ - JWT Auth               │
│ - Router                 │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Route Matching           │
│ Select Upstream Cluster  │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Create Upstream Request  │
│ Send to Backend          │
└────────┬─────────────────┘
         │
         │ Backend responds
         ▼


PHASE 4: RESPONSE PROCESSING
═════════════════════════════
┌──────────────────────────┐
│ Receive Backend Response │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ HTTP Encoder Filters     │
│ (LIFO: C → B → A)        │
│ - Router                 │
│ - JWT Auth               │
│ - CORS                   │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Encode HTTP Response     │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Network Filters          │
│ (Write path - LIFO)      │
│ - HTTP Connection Mgr    │
│ - Rate Limit             │
│ - RBAC                   │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Send to Client           │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Request Complete         │
└──────────────────────────┘


PHASE 5: CONNECTION CLEANUP
════════════════════════════
         │
         │ Connection closes
         ▼
┌──────────────────────────┐
│ Destroy Filter Instances │
│ Call destructors         │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Release Resources        │
│ Update Stats             │
└────────┬─────────────────┘
         │
         ▼
┌──────────────────────────┐
│ Connection Closed        │
└──────────────────────────┘


TIME SCALE:
═══════════
Configuration Phase: Once at startup (milliseconds to seconds)
Connection Phase: Per connection (milliseconds)
Request Phase: Per HTTP stream (milliseconds to seconds)
Response Phase: Per HTTP stream (milliseconds)
Cleanup Phase: Per connection close (milliseconds)
```

## Summary

This document provided comprehensive sequence diagrams and flow charts for:

1. **Configuration Phase**: Listener and filter factory setup
2. **Network Filter Chain**: Connection handling and network filter instantiation
3. **HTTP Filter Chain**: HTTP stream creation and HTTP filter instantiation
4. **Filter Chain Matching**: Algorithm for selecting the correct filter chain
5. **Data Processing**: Network and HTTP filter data flow with iteration
6. **End-to-End Flow**: Complete request lifecycle from config to cleanup

These diagrams complement the previous 9 documents to provide a complete understanding of Envoy's filter chain architecture.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Series Complete**: This concludes the 10-part documentation series on Envoy Filter Chain Creation Flow

## Series Index

1. Overview and Architecture
2. Filter Chain Factory Construction
3. Factory Context Hierarchy
4. Network Filter Chain Creation
5. HTTP Filter Chain Creation
6. Filter Chain Matching and Selection
7. Filter Configuration and Registration
8. Runtime Filter Instantiation
9. UML Class Diagrams
10. Sequence Diagrams and Flow Charts (This Document)

**Total Documentation**: 10 comprehensive documents covering all aspects of filter chain creation in Envoy.
