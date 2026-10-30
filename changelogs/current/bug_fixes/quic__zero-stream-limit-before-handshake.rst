Fixed the HTTP/3 upstream connection pool on a peer stream limit of zero. The pool was never told
about a zero limit, since QUICHE only reports increases, so it kept its optimistic capacity and
could attach a stream QUICHE had no stream ID for. A client that went busy during 0-RTT and then
had its limit raised before the handshake completed also hit an ``ENVOY_BUG`` instead of going
back to accepting early data streams.
