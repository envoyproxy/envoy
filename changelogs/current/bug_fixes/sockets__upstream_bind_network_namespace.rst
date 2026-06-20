Fixed a crash that occurred when an upstream connection was bound to a non-existent Linux network
namespace via :ref:`network_namespace_filepath
<envoy_v3_api_field_config.core.v3.SocketAddress.network_namespace_filepath>` in a cluster's
upstream bind config. Such a cluster is now rejected at config admission time, and if the network
namespace becomes unavailable at runtime the upstream connection now fails gracefully instead of
crashing. This config rejection can be reverted by setting the runtime guard
``envoy.reloadable_features.reject_invalid_bind_network_namespace`` to ``false``.
