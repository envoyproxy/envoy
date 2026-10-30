Fixed the HTTP/3 upstream connection pool during 0-RTT when the stream limit resumed from cached
transport parameters is zero. QUICHE only reports increases, so the pool kept its optimistic capacity
until the handshake completed and could attach a stream QUICHE had no stream ID for. A client that
went busy that way and then had its limit raised before the handshake completed also hit an
``ENVOY_BUG`` instead of going back to accepting early data streams.
