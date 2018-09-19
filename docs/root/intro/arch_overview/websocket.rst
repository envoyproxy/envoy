.. _arch_overview_websocket:

Envoy currently supports two modes of Upgrade behavior, the new generic upgrade mode, and
the old WebSocket-only TCP proxy mode.


New style Upgrade support
=========================

The new style Upgrade support is intended mainly for WebSocket but may be used for non-WebSocket
upgrades as well. The new style of upgrades pass both the HTTP headers and the upgrade payload
through an HTTP filter chain. One may configure the
:ref:`upgrade_configs <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.upgrade_configs>`
in one of two ways. If only the
:ref:`upgrade_type <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.UpgradeConfig.upgrade_type>`
is specified, both the upgrade headers, any request and response body, and WebSocket payload will
pass through the default HTTP filter chain. To avoid the use of HTTP-only filters for upgrade payload,
one can set up custom
:ref:`filters <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.UpgradeConfig.filters>`
for the given upgrade type, up to and including only using the router filter to send the WebSocket
data upstream.

Handling H2 hops (implementation in progress)
---------------------------------------------

Envoy currently has an alpha implementation of tunneling websockets over H2 streams for deployments
that prefer a uniform H2 mesh throughout, for example, for a deployment of the form:

[Client] ---- HTTP/1.1 ---- [Front Envoy] ---- HTTP/2 ---- [Sidecar Envoy ---- H1  ---- App]

In this case, if a client is for example using WebSocket, we want the Websocket to arive at the
upstream server functionally intact, which means it needs to traverse the HTTP/2 hop.

TODO(alyssawilk) copy the warnings from the config here, or just land the docs when we unhide.

This is accomplished via
`extended CONNECT <https://tools.ietf.org/html/draft-mcmanus-httpbis-h2-websockets>`_ support. The
WebSocket request will be transformed into an HTTP/2 CONNECT stream, with :protocol header
indicating the original upgrade, traverse the HTTP/2 hop, and be downgraded back into an HTTP/1
WebSocket Upgrade. This same Upgrade-CONNECT-Upgrade transformation will be performed on any
HTTP/2 hop, with the documented flaw that the HTTP/1.1 method is always assumed to be GET.
Non-WebSocket upgrades are allowed to use any valid HTTP method (i.e. POST) and the current
upgrade/downgrade mechanism will drop the original method and transform the Upgrade request to
a GET method on the final Envoy-Upstream hop.

Old style WebSocket support
===========================

Envoy supports upgrading a HTTP/1.1 connection to a WebSocket connection.
Connection upgrade will be allowed only if the downstream client
sends the correct upgrade headers and the matching HTTP route is explicitly
configured to use WebSockets
(:ref:`use_websocket <envoy_api_field_route.RouteAction.use_websocket>`).
If a request arrives at a WebSocket enabled route without the requisite
upgrade headers, it will be treated as any regular HTTP/1.1 request.

Since Envoy treats WebSocket connections as plain TCP connections, it
supports all drafts of the WebSocket protocol, independent of their wire
format. Certain HTTP request level features such as redirects, timeouts,
retries, rate limits and shadowing are not supported for WebSocket routes.
However, prefix rewriting, explicit and automatic host rewriting, traffic
shifting and splitting are supported.

Old style Connection semantics
------------------------------

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
