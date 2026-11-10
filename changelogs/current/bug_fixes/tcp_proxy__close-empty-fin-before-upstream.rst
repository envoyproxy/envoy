Fixed :ref:`tcp_proxy <config_network_filters_tcp_proxy>` in
``upstream_connect_mode: ON_DOWNSTREAM_DATA`` leaving a downstream connection open until
``idle_timeout`` when the client half-closed (FIN / ``end_stream``) without sending any data.
Because half-close is enabled, that FIN is not reported as ``RemoteClose``, and tcp_proxy
intentionally does not open an upstream for an empty FIN, so nothing previously closed the
session. The filter now closes the downstream connection in that case. Modes that already have
an upstream connection in flight (``IMMEDIATE``, or ``ON_DOWNSTREAM_TLS_HANDSHAKE`` after the
handshake) are unchanged and still flush the empty ``end_stream`` to upstream when it connects.
