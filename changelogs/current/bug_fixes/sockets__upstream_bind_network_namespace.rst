Fixed a crash that occurred when an upstream connection was bound to an unavailable Linux network
namespace via :ref:`network_namespace_filepath
<envoy_v3_api_field_config.core.v3.SocketAddress.network_namespace_filepath>` in a cluster's
upstream bind config. The failure to create the connection is now surfaced by the connection pools
as a connection failure instead of crashing.
