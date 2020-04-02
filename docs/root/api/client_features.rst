.. _client_features:

Well Known Client Features
==========================

Authoritative list of features that an xDS client may support. An xDS client supplies the list of
features it supports in the :ref:`client_features <envoy_api_field_core.Node.client_features>` field.
Client features use reverse DNS naming scheme, for example `com.acme.feature`.

Currently Defined Client Features
---------------------------------

.. It would be nice to use an RST ref here for service.load_stats.v2.LoadStatsResponse.send_all_clusters, but we can't due to https://github.com/envoyproxy/envoy/issues/3091.

- **envoy.config.require-any-fields-contain-struct**: This feature indicates that xDS client
  requires that the configuration entries of type  *google.protobuf.Any* contain messages of type
  *udpa.type.v1.TypedStruct* only.
- **envoy.lb.does_not_support_overprovisioning**: This feature indicates that the client does not
  support overprovisioning for priority failover and locality weighting as configured by the
  :ref:`overprovisioning_factor<envoy_api_field_ClusterLoadAssignment.Policy.overprovisioning_factor>`
  field. If graceful failover functionality is required, it must be supplied by the management
  server.
- **envoy.lrs.supports_send_all_clusters**: This feature indicates that the client supports
  the *envoy_api_field_service.load_stats.v2.LoadStatsResponse.send_all_clusters*
  field in the LRS response.
