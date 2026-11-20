Added the network filter ABI callback
``envoy_dynamic_module_callback_network_filter_set_transport_socket_data_chunk_send_limit``, so a
dynamic module network filter can cap the plaintext Envoy passes to each downstream TLS write, and
therefore the plaintext size of each TLS record it sends. The default stays 16384 bytes. The Rust
SDK exposes this as ``EnvoyNetworkFilter::set_transport_socket_data_chunk_send_limit``. The limit
is carried by the new ``Network::Connection::setTransportSocketDataChunkSendLimit()`` and
``Network::TransportSocket::setTlsDataChunkSendLimit()`` methods, which the TLS, StartTLS and
wrapping transport sockets implement.
