Added support for implementing a custom TLS handshaker as a dynamic module through the
:ref:`dynamic module TLS handshaker
<envoy_v3_api_msg_extensions.transport_sockets.tls.handshakers.dynamic_modules.v3.DynamicModuleTlsHandshaker>`
extension. A module can declare the handshaker capabilities, configure the ``SSL_CTX``, and drive
the TLS handshake. This works for both client and server TLS contexts.
