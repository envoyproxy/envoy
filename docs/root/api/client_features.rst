.. _client_features:

Well Known Client Features
==========================

Authoritative list of features that an xDS client may support. An xDS client supplies the list of
features it supports in the :ref:`client_features <envoy_v3_api_field_config.core.v3.node.client_features>` field.
Client features use reverse DNS naming scheme, for example ``com.acme.feature``.

Currently Defined Client Features
---------------------------------

.. It would be nice to use an RST ref here for service.load_stats.v2.LoadStatsResponse.send_all_clusters, but we can't due to https://github.com/envoyproxy/envoy/issues/3091.

- **envoy.config.require-any-fields-contain-struct**: This feature indicates that xDS client
  requires that the configuration entries of type  *google.protobuf.Any* contain messages of type
  *xds.type.v3.TypedStruct* (or, for historical reasons, *udpa.type.v1.TypedStruct*) only.
- **envoy.lb.does_not_support_overprovisioning**: This feature indicates that the client does not
  support overprovisioning for priority failover and locality weighting as configured by the
  :ref:`overprovisioning_factor <envoy_v3_api_field_config.endpoint.v3.clusterloadassignment.policy.overprovisioning_factor>`
  field. If graceful failover functionality is required, it must be supplied by the management
  server.
- **envoy.lrs.supports_send_all_clusters**: This feature indicates that the client supports
  the *envoy_api_field_service.load_stats.v2.LoadStatsResponse.send_all_clusters*
  field in the LRS response.
- **xds.config.supports-resource-ttl**: This feature indicates that xDS client supports
  per-resource or per SotW :ref:`TTL <xds_protocol_TTL>`.
- **xds.config.resource-in-sotw**: This feature indicates that the xDS client is able to unwrap
  *Resource* wrappers within the SotW DiscoveryResponse.
