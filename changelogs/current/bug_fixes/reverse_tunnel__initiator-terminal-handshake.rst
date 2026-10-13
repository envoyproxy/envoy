Hardened reverse tunnel initiation in the :ref:`downstream reverse tunnel socket interface
<envoy_v3_api_msg_extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface>`.
A dial that opens the connection but never receives a handshake response is now closed after the new
``handshake_timeout`` (default ``15s``) rather than holding the reverse tunnel slot forever. A
handshake that fails synchronously or whose connection closes before completion is now terminal and
installs backoff, and backoff is cleared only on a verified success. The requested host is selected
strictly, so an unhealthy host is no longer dialed under another host's key. Bytes the responder
coalesces with the handshake response are carried into the accepted tunnel and replayed before the
socket, and a queued connection that closes before ``accept()`` releases its tunnel key. Terminal
handshake states are no longer stored per host, bounding memory under connection churn.
