.. _arch_overview_websocket:

WebSocket support
=================

Envoy supports upgrading a HTTP/1.1 connection to a WebSocket connection.
Connection upgrade will be allowed only if the downstream client
sends the correct upgrade headers and the matching HTTP route is explicitly
configured to use WebSockets
(:ref:`use_websocket <config_http_conn_man_route_table_route_use_websocket>`).
If a request arrives at a WebSocket enabled route without the requisite
upgrade headers, it will be treated as any regular HTTP/1.1 request.

Since Envoy treats WebSocket connections as plain TCP connections, it
supports all drafts of the WebSocket protocol, independent of their wire
format. Certain HTTP request level features such as redirects, timeouts,
retries, rate limits and shadowing are not supported for WebSocket routes.
However, prefix rewriting, explicit and automatic host rewriting, traffic
shifting and splitting are supported.

Connection semantics
--------------------

Even though WebSocket upgrades occur over HTTP/1.1 connections, WebSockets
proxying works similarly to plain TCP proxy, i.e., Envoy does not interpret
the websocket frames. The downstream client and/or the upstream server are
responsible for properly terminating the WebSocket connection
(e.g., by sending `close frames <https://tools.ietf.org/html/rfc6455#section-5.5.1>`_)
and the underlying TCP connection.

When the connection manager receives a WebSocket upgrade request over a
WebSocket-enabled route, it forwards the request to an upstream server over a
TCP connection. Envoy will not know if the upstream server rejected the upgrade
request. It is the responsibility of the upstream server to terminate the TCP
connection, which would cause Envoy to terminate the corresponding downstream
client connection.
