Added a ``handshake_type`` oneof to the reverse tunnel initiator
(``DownstreamReverseConnectionSocketInterface``) and responder (``ReverseTunnel`` filter)
configurations, so the reverse tunnel handshake can be provided by an extension registered under
the ``envoy.reverse_tunnel.initiator_handshake`` or ``envoy.reverse_tunnel.responder_handshake``
category. The built-in HTTP/1.1 handshake remains the default, and the existing configuration
fields keep their meaning.
