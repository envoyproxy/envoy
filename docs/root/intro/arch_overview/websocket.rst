.. _arch_overview_websocket:

WebSocket and HTTP upgrades
===========================

Envoy Upgrade support is intended mainly for WebSocket but may be used for non-WebSocket
upgrades as well. Upgrades pass both the HTTP headers and the upgrade payload
through an HTTP filter chain. One may configure the
:ref:`upgrade_configs <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.upgrade_configs>`
with or without custom filter chains. If only the
:ref:`upgrade_type <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.UpgradeConfig.upgrade_type>`
is specified, both the upgrade headers, any request and response body, and WebSocket payload will
pass through the default HTTP filter chain. To avoid the use of HTTP-only filters for upgrade payload,
one can set up custom
:ref:`filters <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.UpgradeConfig.filters>`
for the given upgrade type, up to and including only using the router filter to send the WebSocket
data upstream.

Upgrades can be enabled or disabled on a :ref:`per-route <envoy_api_field_route.RouteAction.upgrade_configs>` basis.
Any per-route enabling/disabling automatically overrides HttpConnectionManager configuration as
laid out below, but custom filter chains can only be configured on a per-HttpConnectionManager basis.

+-----------------------+-------------------------+-------------------+
| *HCM Upgrade Enabled* | *Route Upgrade Enabled* | *Upgrade Enabled* |
+=======================+=========================+===================+
| T (Default)           | T (Default)             | T                 |
+-----------------------+-------------------------+-------------------+
| T (Default)           | F                       | F                 |
+-----------------------+-------------------------+-------------------+
| F                     | T (Default)             | T                 |
+-----------------------+-------------------------+-------------------+
| F                     | F                       | F                 |
+-----------------------+-------------------------+-------------------+

Note that the statistics for upgrades are all bundled together so websocket
:ref:`statistics <config_http_conn_man_stats>` are tracked by stats such as
downstream_cx_upgrades_total and downstream_cx_upgrades_active

Handling H2 hops
^^^^^^^^^^^^^^^^

Envoy currently has an alpha implementation of tunneling websockets over H2 streams for deployments
that prefer a uniform H2 mesh throughout, for example, for a deployment of the form:

[Client] ---- HTTP/1.1 ---- [Front Envoy] ---- HTTP/2 ---- [Sidecar Envoy ---- H1  ---- App]

In this case, if a client is for example using WebSocket, we want the Websocket to arive at the
upstream server functionally intact, which means it needs to traverse the HTTP/2 hop.

This is accomplished via
`extended CONNECT <https://tools.ietf.org/html/draft-mcmanus-httpbis-h2-websockets>`_ support. The
WebSocket request will be transformed into an HTTP/2 CONNECT stream, with :protocol header
indicating the original upgrade, traverse the HTTP/2 hop, and be downgraded back into an HTTP/1
WebSocket Upgrade. This same Upgrade-CONNECT-Upgrade transformation will be performed on any
HTTP/2 hop, with the documented flaw that the HTTP/1.1 method is always assumed to be GET.
Non-WebSocket upgrades are allowed to use any valid HTTP method (i.e. POST) and the current
upgrade/downgrade mechanism will drop the original method and transform the Upgrade request to
a GET method on the final Envoy-Upstream hop.

Note that the H2 upgrade path has very strict HTTP/1.1 compliance, so will not proxy WebSocket
upgrade requests or responses with bodies.
