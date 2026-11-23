Fixed an ``ENVOY_BUG`` and premature transition to ``Ready`` in upstream HTTP/3 connection pools when a 0-RTT
connection exhausted its cached stream limit (entering ``Busy``) and the server subsequently advertised a higher
``initial_max_streams_bidi`` during the TLS handshake before handshake completion. The connection now transitions back
to ``ReadyForEarlyData`` until the handshake finishes.
