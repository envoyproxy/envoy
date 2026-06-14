When the runtime guard ``envoy.reloadable_features.web_transport`` is enabled, a ``CONNECT``
request with ``:protocol`` set to ``webtransport`` is no longer coerced into an HTTP/1 ``Upgrade``
(the single-bytestream tunneling form used for WebSocket and other extended ``CONNECT`` protocols),
since that model cannot represent a WebTransport session. This behavior is gated by the guard,
which defaults to ``false``.
