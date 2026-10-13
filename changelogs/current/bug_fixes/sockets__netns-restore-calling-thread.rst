Fixed the network namespace switch used for listeners and client connections with a
:ref:`network_namespace_filepath <envoy_v3_api_field_config.core.v3.SocketAddress.network_namespace_filepath>`
to return the calling thread to its own original namespace. It previously restored the main thread's current
namespace, so a worker thread creating a connection while the main thread was inside another namespace (for
example creating a listener or a health check connection there) could be left in that namespace until its next
switch, creating its sockets in the wrong namespace in the meantime.
