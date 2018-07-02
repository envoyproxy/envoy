How does Envoy handle Upgrades (especially WebSocket)?
======================================================


Envoy currently supports two modes of Upgrade behavior.

The first is the deprecated WebSocket-only upgrade path. This code path works for HTTP/1 downstream
and upstreams. It detects requests with "Upgrade: websocket" headers and upgrades from HTTP processing
mode to TCP proxy mode, establishing a raw TCP connection upstream, forwarding the raw headers, any
HTTP body, and WebSocket payload unmodified by the HTTP filter chain.

The new style of upgrades, supports any configured type of upgrade, including WebSocket.
The current implementation handles HTTP/1.1 downstream and upstream, but H2 support is
`planned <https://github.com/envoyproxy/envoy/issues/1630>`_ via
`extended connect <https://tools.ietf.org/html/draft-mcmanus-httpbis-h2-ebsockets-02>`_
The new style of upgrades pass both the HTTP headers and the upgrade payload through an HTTP filter
chain. One may configure a separate filter chain per upgrade type via the HttpConnectionManager
:ref:`upgrade_configs <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.upgrade_configs>`
to avoid HTTP-only filters interacting with WebSocket or other Upgrade payload.
