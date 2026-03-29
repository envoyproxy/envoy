# xDS and Dynamic Configuration Classes

This folder documents Envoy's xDS (Discovery Service) and dynamic configuration subsystems. All classes are verified against the source code.

## Source Paths

| Component | Path |
|-----------|------|
| Subscription | `envoy/config/subscription.h`, `source/common/config/` |
| GrpcMux | `envoy/config/grpc_mux.h`, `source/extensions/config_subscription/grpc/` |
| ConfigProvider | `envoy/config/config_provider.h`, `source/common/config/` |
| XdsManager | `envoy/config/xds_manager.h`, `source/common/config/` |

## Key Documents

- [104-Subscription](104-Subscription.md) — Subscription, SubscriptionCallbacks, GrpcSubscriptionImpl
- [105-GrpcMux](105-GrpcMux.md) — GrpcMux, GrpcMuxSotw, GrpcMuxDelta
- [106-ConfigProvider](106-ConfigProvider.md) — ConfigProvider, ConfigProviderManager, ConfigSubscriptionInstance
