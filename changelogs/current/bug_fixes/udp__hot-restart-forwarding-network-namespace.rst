Fixed hot restart UDP packet forwarding for listeners bound with
:ref:`network_namespace_filepath <envoy_v3_api_field_config.core.v3.SocketAddress.network_namespace_filepath>`.
The draining parent now forwards the listener's bind address and network namespace with each datagram and the
child resolves the target listener from them, so listeners sharing an address across namespaces, and
``transparent`` listeners whose datagram destination differs from the bind address, receive their own forwarded
datagrams instead of all being delivered to the first registered listener on that address.
