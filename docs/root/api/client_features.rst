.. _client_features:

众所周知的客户端功能
========================

xDS 客户端可能支持的功能的权威列表。xDS 客户端在 :ref:`client_features <envoy_api_field_core.Node.client_features>` 字段中提供了它支持的功能列表。客户端功能使用反向 DNS 命名方案，例如 `com.acme.feature`。

当前定义的客户端功能
-----------------------

.. It would be nice to use an RST ref here for service.load_stats.v2.LoadStatsResponse.send_all_clusters, but we can't due to https://github.com/envoyproxy/envoy/issues/3091.

- **envoy.config.require-any-fields-contain-struct**: 此功能表明 xDS 客户端要求 *google.protobuf.Any* 类型的配置条目仅包含 *udpa.type.v1.TypedStruct* 类型的消息。
- **envoy.lb.does_not_support_overprovisioning**: 此功能表明客户端不支持通过 :ref:`overprovisioning_factor<envoy_api_field_ClusterLoadAssignment.Policy.overprovisioning_factor>` 字段配置的优先级故障转移和位置加权的过度配置。如果需要正常的故障转移功能，则必须由管理服务器提供。
- **envoy.lrs.supports_send_all_clusters**: 此功能表明客户端支持 LRS 响应中的 *envoy_api_field_service.load_stats.v2.LoadStatsResponse.send_all_clusters* 字段。