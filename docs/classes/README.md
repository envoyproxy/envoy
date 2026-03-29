# Envoy Important Classes — 100-Part Reference

This document series catalogs the **most important classes** in the Envoy codebase, with UML diagrams, key function summaries, and brief class descriptions. All classes are **verified against the actual source code** — no fictional classes.

## Organization by Domain

| Folder | Parts | Domain | Key Classes |
|--------|-------|--------|-------------|
| [01-network](01-network/) | 1–15 | Network layer | ConnectionImpl, FilterManagerImpl, Connection, ListenerFilterBuffer |
| [02-http](02-http/) | 16–35 | HTTP layer | ConnectionManagerImpl, FilterManager, CodecClient, StreamDecoder |
| [03-upstream](03-upstream/) | 36–55 | Upstream/clusters | ClusterManagerImpl, HostImpl, ClusterImplBase, LoadBalancer |
| [04-listener](04-listener/) | 56–70 | Listeners | ListenerImpl, ActiveTcpListener, FilterChainManagerImpl, ConnectionHandlerImpl |
| [05-server](05-server/) | 71–85 | Server/runtime | InstanceImpl, WorkerImpl, AdminImpl, DrainManagerImpl |
| [06-config-router](06-config-router/) | 86–100 | Config & router | ConfigurationImpl, Router::Filter, RdsRouteConfigProvider |
| [07-observability](07-observability/) | 101–103 | Observability | Tracer, AccessLog, Stats |
| [08-xds](08-xds/) | 104–105 | xDS | Subscription, GrpcMux |
| [09-supporting](09-supporting/) | 107–111 | Supporting | StreamInfo, Runtime, Event, Init, ThreadLocal |

## Full Index (Parts 1–100)

### 01-network (Parts 1–15)
| Part | Document | Classes |
|------|----------|---------|
| 1 | [001-ConnectionImpl](01-network/001-ConnectionImpl.md) | ConnectionImpl |
| 2 | [002-Connection-Interface](01-network/002-Connection-Interface.md) | Connection, ConnectionCallbacks |
| 3 | [003-FilterManagerImpl](01-network/003-FilterManagerImpl.md) | FilterManagerImpl |
| 4 | [004-FilterManagerConnection](01-network/004-FilterManagerConnection.md) | FilterManagerConnection |
| 5 | [005-ConnectionImplBase](01-network/005-ConnectionImplBase.md) | ConnectionImplBase |
| 6 | [006-ReadFilter-WriteFilter](01-network/006-ReadFilter-WriteFilter.md) | ReadFilter, WriteFilter |
| 7 | [007-ListenerFilter](01-network/007-ListenerFilter.md) | ListenerFilter, ListenerFilterCallbacks |
| 8 | [008-ListenerFilterBuffer](01-network/008-ListenerFilterBuffer.md) | ListenerFilterBufferImpl |
| 9 | [009-ConnectionSocket](01-network/009-ConnectionSocket.md) | ConnectionSocket |
| 10 | [010-TransportSocket](01-network/010-TransportSocket.md) | TransportSocket, TransportSocketCallbacks |
| 11 | [011-ConnectionHandler](01-network/011-ConnectionHandler.md) | ConnectionHandler |
| 12 | [012-Address](01-network/012-Address.md) | Address, SocketAddress |
| 13 | [013-IoHandle](01-network/013-IoHandle.md) | IoHandle |
| 14 | [014-TcpListener](01-network/014-TcpListener.md) | TcpListenerCallbacks, TcpListenerImpl |
| 15 | [015-ConnectionEvent](01-network/015-ConnectionEvent.md) | ConnectionEvent, ConnectionCloseType |

### 02-http (Parts 16–35)
| Part | Document | Classes |
|------|----------|---------|
| 16 | [016-ConnectionManagerImpl](02-http/016-ConnectionManagerImpl.md) | ConnectionManagerImpl |
| 17 | [017-ConnectionManagerConfig](02-http/017-ConnectionManagerConfig.md) | ConnectionManagerConfig |
| 18 | [018-HTTP-FilterManager](02-http/018-HTTP-FilterManager.md) | FilterManager (HTTP) |
| 19 | [019-CodecClient](02-http/019-CodecClient.md) | CodecClient |
| 20 | [020-StreamDecoder-Encoder](02-http/020-StreamDecoder-Encoder.md) | StreamDecoder, StreamEncoder |
| 21 | [021-Codec-Connection](02-http/021-Codec-Connection.md) | Codec, ServerConnection, ClientConnection |
| 22 | [022-StreamDecoderFilter](02-http/022-StreamDecoderFilter.md) | StreamDecoderFilter |
| 23 | [023-StreamEncoderFilter](02-http/023-StreamEncoderFilter.md) | StreamEncoderFilter |
| 24 | [024-Stream-StreamCallbacks](02-http/024-Stream-StreamCallbacks.md) | Stream, StreamCallbacks |
| 25 | [025-HeaderMap](02-http/025-HeaderMap.md) | HeaderMap |
| 26 | [026-RequestEncoder-ResponseDecoder](02-http/026-RequestEncoder-ResponseDecoder.md) | RequestEncoder, ResponseDecoder |
| 27 | [027-FilterStatus-Enums](02-http/027-FilterStatus-Enums.md) | FilterHeadersStatus, FilterDataStatus |
| 28 | [028-StreamFilterCallbacks](02-http/028-StreamFilterCallbacks.md) | StreamFilterCallbacks |
| 29 | [029-AsyncClient](02-http/029-AsyncClient.md) | AsyncClient |
| 30 | [030-FilterChainFactory](02-http/030-FilterChainFactory.md) | FilterChainFactory |
| 31 | [031-Http1StreamEncoderOptions](02-http/031-Http1StreamEncoderOptions.md) | Http1StreamEncoderOptions |
| 32 | [032-MetadataMap](02-http/032-MetadataMap.md) | MetadataMap |
| 33 | [033-Http-Status](02-http/033-Http-Status.md) | Http::Status |
| 34 | [034-Http-Context](02-http/034-Http-Context.md) | Http::Context |
| 35 | [035-ConnectionCallbacks](02-http/035-ConnectionCallbacks.md) | ConnectionCallbacks |

### 03-upstream (Parts 36–55)
| Part | Document | Classes |
|------|----------|---------|
| 36 | [036-ClusterManagerImpl](03-upstream/036-ClusterManagerImpl.md) | ClusterManagerImpl |
| 37 | [037-Cluster-Host](03-upstream/037-Cluster-Host.md) | Cluster, Host |
| 38 | [038-HostImpl](03-upstream/038-HostImpl.md) | HostImpl |
| 39 | [039-ClusterImplBase](03-upstream/039-ClusterImplBase.md) | ClusterImplBase |
| 40 | [040-LoadBalancer](03-upstream/040-LoadBalancer.md) | LoadBalancer |
| 41 | [041-ClusterInfo](03-upstream/041-ClusterInfo.md) | ClusterInfo |
| 42 | [042-PrioritySet](03-upstream/042-PrioritySet.md) | PrioritySet |
| 43 | [043-HostSet](03-upstream/043-HostSet.md) | HostSet |
| 44 | [044-HealthChecker](03-upstream/044-HealthChecker.md) | HealthChecker |
| 45 | [045-OutlierDetector](03-upstream/045-OutlierDetector.md) | Outlier::Detector |
| 46 | [046-ThreadLocalCluster](03-upstream/046-ThreadLocalCluster.md) | ThreadLocalCluster |
| 47 | [047-ConnectionPool](03-upstream/047-ConnectionPool.md) | ConnectionPool::Instance |
| 48 | [048-HostDescription](03-upstream/048-HostDescription.md) | HostDescription |
| 49 | [049-ClusterManager](03-upstream/049-ClusterManager.md) | ClusterManager |
| 50 | [050-ResourceManager](03-upstream/050-ResourceManager.md) | ResourceManager |
| 51 | [051-Locality](03-upstream/051-Locality.md) | Locality |
| 52 | [052-HealthFlag](03-upstream/052-HealthFlag.md) | HealthFlag |
| 53 | [053-ClusterManagerFactory](03-upstream/053-ClusterManagerFactory.md) | ClusterManagerFactory |
| 54 | [054-CdsApi](03-upstream/054-CdsApi.md) | CdsApi |
| 55 | [055-HostStats](03-upstream/055-HostStats.md) | HostStats |

### 04-listener (Parts 56–70)
| Part | Document | Classes |
|------|----------|---------|
| 56 | [056-ListenerImpl](04-listener/056-ListenerImpl.md) | ListenerImpl |
| 57 | [057-ListenerManagerImpl](04-listener/057-ListenerManagerImpl.md) | ListenerManagerImpl |
| 58 | [058-ActiveTcpListener](04-listener/058-ActiveTcpListener.md) | ActiveTcpListener |
| 59 | [059-FilterChainManagerImpl](04-listener/059-FilterChainManagerImpl.md) | FilterChainManagerImpl |
| 60 | [060-ActiveTcpSocket](04-listener/060-ActiveTcpSocket.md) | ActiveTcpSocket |
| 61 | [061-ConnectionHandlerImpl](04-listener/061-ConnectionHandlerImpl.md) | ConnectionHandlerImpl |
| 62 | [062-ListenerConfig](04-listener/062-ListenerConfig.md) | ListenerConfig |
| 63 | [063-FilterChain](04-listener/063-FilterChain.md) | FilterChain |
| 64 | [064-FilterChainFactory](04-listener/064-FilterChainFactory.md) | FilterChainFactory |
| 65 | [065-ListenerFilterChainFactoryBuilder](04-listener/065-ListenerFilterChainFactoryBuilder.md) | ListenerFilterChainFactoryBuilder |
| 66 | [066-OriginalDstFilter](04-listener/066-OriginalDstFilter.md) | OriginalDstFilter |
| 67 | [067-ProxyProtocolFilter](04-listener/067-ProxyProtocolFilter.md) | ProxyProtocol::Filter |
| 68 | [068-FilterChainImpl](04-listener/068-FilterChainImpl.md) | FilterChainImpl |
| 69 | [069-FilterChainFactoryBuilder](04-listener/069-FilterChainFactoryBuilder.md) | FilterChainFactoryBuilder |
| 70 | [070-TcpListenerCallbacks](04-listener/070-TcpListenerCallbacks.md) | TcpListenerCallbacks |

### 05-server (Parts 71–85)
| Part | Document | Classes |
|------|----------|---------|
| 71 | [071-InstanceImpl](05-server/071-InstanceImpl.md) | InstanceImpl |
| 72 | [072-WorkerImpl](05-server/072-WorkerImpl.md) | WorkerImpl |
| 73 | [073-AdminImpl](05-server/073-AdminImpl.md) | AdminImpl |
| 74 | [074-ConfigurationImpl](05-server/074-ConfigurationImpl.md) | MainImpl |
| 75 | [075-DrainManagerImpl](05-server/075-DrainManagerImpl.md) | DrainManagerImpl |
| 76 | [076-ServerLifecycleNotifier](05-server/076-ServerLifecycleNotifier.md) | ServerLifecycleNotifier |
| 77 | [077-OptionsImpl](05-server/077-OptionsImpl.md) | OptionsImpl |
| 78 | [078-Bootstrap](05-server/078-Bootstrap.md) | Bootstrap |
| 79 | [079-Main](05-server/079-Main.md) | Configuration::Main |
| 80 | [080-Instance](05-server/080-Instance.md) | Instance |
| 81 | [081-Worker](05-server/081-Worker.md) | Worker |
| 82 | [082-Admin](05-server/082-Admin.md) | Admin |
| 83 | [083-DrainManager](05-server/083-DrainManager.md) | DrainManager |
| 84 | [084-Options](05-server/084-Options.md) | Options |
| 85 | [085-Watchdog](05-server/085-Watchdog.md) | Watchdog |

### 07-observability (Parts 101–103)
| Part | Document | Classes |
|------|----------|---------|
| 101 | [101-Tracing](07-observability/101-Tracing.md) | Tracer, Span, Driver |
| 102 | [102-AccessLog](07-observability/102-AccessLog.md) | AccessLog::Instance, Filter, AccessLogManager |
| 103 | [103-Stats](07-observability/103-Stats.md) | Store, Counter, Gauge, Histogram |

### 08-xds (Parts 104–105)
| Part | Document | Classes |
|------|----------|---------|
| 104 | [104-Subscription](08-xds/104-Subscription.md) | Subscription, SubscriptionCallbacks |
| 105 | [105-GrpcMux](08-xds/105-GrpcMux.md) | GrpcMux, GrpcMuxSotw, GrpcMuxDelta |

### 09-supporting (Parts 107–111)
| Part | Document | Classes |
|------|----------|---------|
| 107 | [107-StreamInfo](09-supporting/107-StreamInfo.md) | StreamInfo, StreamInfoImpl |
| 108 | [108-Runtime](09-supporting/108-Runtime.md) | Runtime::Loader, Snapshot |
| 109 | [109-Event](09-supporting/109-Event.md) | Event::Dispatcher, Timer |
| 110 | [110-Init](09-supporting/110-Init.md) | Init::Manager, Init::Target |
| 111 | [111-ThreadLocal](09-supporting/111-ThreadLocal.md) | ThreadLocal::Slot, TypedSlot |

### 06-config-router (Parts 86–100)
| Part | Document | Classes |
|------|----------|---------|
| 86 | [086-ConfigurationImpl](06-config-router/086-ConfigurationImpl.md) | ConfigImpl |
| 87 | [087-FactoryContext](06-config-router/087-FactoryContext.md) | FactoryContext |
| 88 | [088-Router-Filter](06-config-router/088-Router-Filter.md) | Router::Filter |
| 89 | [089-RdsRouteConfigProvider](06-config-router/089-RdsRouteConfigProvider.md) | RouteConfigProvider |
| 90 | [090-RouteConfigProvider](06-config-router/090-RouteConfigProvider.md) | RouteConfigProvider |
| 91 | [091-Route](06-config-router/091-Route.md) | Route |
| 92 | [092-RouteEntry](06-config-router/092-RouteEntry.md) | RouteEntry |
| 93 | [093-FilterChainUtility](06-config-router/093-FilterChainUtility.md) | FilterChainUtility |
| 94 | [094-ConfigProviderManager](06-config-router/094-ConfigProviderManager.md) | ConfigProviderManager |
| 95 | [095-RouteEntryImplBase](06-config-router/095-RouteEntryImplBase.md) | RouteEntryImplBase |
| 96 | [096-RouteConfigProviderManager](06-config-router/096-RouteConfigProviderManager.md) | RouteConfigProviderManager |
| 97 | [097-VirtualHost](06-config-router/097-VirtualHost.md) | VirtualHost |
| 98 | [098-RouteMatcher](06-config-router/098-RouteMatcher.md) | RouteMatcher |
| 99 | [099-RetryPolicy](06-config-router/099-RetryPolicy.md) | RetryPolicy |
| 100 | [100-ConfigProvider](06-config-router/100-ConfigProvider.md) | ConfigProvider |

## C++ Patterns Guide

For common C++ patterns used in `source/`, see [../cpp-patterns/](../cpp-patterns/).

## How to Use

1. **Find a class** — Use the index above or search by domain folder.
2. **Understand relationships** — Each doc includes a UML class diagram (Mermaid).
3. **Key functions** — Important methods with one-line descriptions.
4. **Quick summary** — Brief explanation of the class role.

## Source Paths

- **Network:** `source/common/network/`, `envoy/network/`
- **HTTP:** `source/common/http/`, `envoy/http/`
- **Upstream:** `source/common/upstream/`, `envoy/upstream/`
- **Listener:** `source/common/listener_manager/`, `envoy/network/`, `envoy/server/`
- **Server:** `source/server/`, `envoy/server/`
- **Router:** `source/common/router/`, `envoy/router/`
