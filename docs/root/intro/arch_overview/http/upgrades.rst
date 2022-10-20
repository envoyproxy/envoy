.. _arch_overview_upgrades:

HTTP upgrades
===========================

Envoy Upgrade support is intended mainly for WebSocket and CONNECT support, but may be used for
arbitrary upgrades as well. Upgrades pass both the HTTP headers and the upgrade payload
through an HTTP filter chain. One may configure the
:ref:`upgrade_configs <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.upgrade_configs>`
with or without custom filter chains. If only the
:ref:`upgrade_type <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.upgrade_type>`
is specified, both the upgrade headers, any request and response body, and HTTP data payload will
pass through the default HTTP filter chain. To avoid the use of HTTP-only filters for upgrade payload,
one can set up custom
:ref:`filters <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.filters>`
for the given upgrade type, up to and including only using the router filter to send the HTTP
data upstream. Note that buffering is generally not compatible with upgrades, so if the
:ref:`Buffer filter <envoy_v3_api_msg_extensions.filters.http.buffer.v3.Buffer>` is configured in
the default HTTP filter chain it should probably be excluded for upgrades by using
:ref:`upgrade filters <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig.filters>`
and not including the buffer filter in that list.

Upgrades can be enabled or disabled on a :ref:`per-route <envoy_v3_api_field_config.route.v3.RouteAction.upgrade_configs>` basis.
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

Note that the statistics for upgrades are all bundled together so WebSocket and other upgrades
:ref:`statistics <config_http_conn_man_stats>` are tracked by stats such as
downstream_cx_upgrades_total and downstream_cx_upgrades_active

Websocket over HTTP/2 or HTTP/3 hops
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

While HTTP/2 and HTTP/3 support for WebSockets is off by default, Envoy does support tunneling WebSockets over
HTTP/2 and above for deployments that prefer a uniform HTTP/2+ mesh throughout; this enables, for example,
a deployment of the form:

[Client] ---- HTTP/1.1 ---- [Front Envoy] ---- HTTP/2 ---- [Sidecar Envoy ---- H1  ---- App]

In this case, if a client is for example using WebSocket, we want the Websocket to arrive at the
upstream server functionally intact, which means it needs to traverse the HTTP/2+ hop.

This is accomplished for HTTP/2 via `Extended CONNECT (RFC8441) <https://tools.ietf.org/html/rfc8441>`_ support,
turned on by setting :ref:`allow_connect <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.allow_connect>`
true at the second layer Envoy. For HTTP/3 there is parallel support configured by the alpha option
:ref:`allow_extended_connect <envoy_v3_api_field_config.core.v3.Http3ProtocolOptions.allow_extended_connect>` as
there is no formal RFC yet.

The WebSocket request will be transformed into an HTTP/2+ CONNECT stream, with :protocol header
indicating the original upgrade, traverse the HTTP/2+ hop, and be downgraded back into an HTTP/1
WebSocket Upgrade. This same Upgrade-CONNECT-Upgrade transformation will be performed on any
HTTP/2+ hop, with the documented flaw that the HTTP/1.1 method is always assumed to be GET.
Non-WebSocket upgrades are allowed to use any valid HTTP method (i.e. POST) and the current
upgrade/downgrade mechanism will drop the original method and transform the Upgrade request to
a GET method on the final Envoy-Upstream hop.

Note that the HTTP/2+ upgrade path has very strict HTTP/1.1 compliance, so will not proxy WebSocket
upgrade requests or responses with bodies.

CONNECT support
^^^^^^^^^^^^^^^

Envoy CONNECT support is off by default (Envoy will send an internally generated 403 in response to
CONNECT requests). CONNECT support can be enabled via the upgrade options described above, setting
the upgrade value to the special keyword "CONNECT".

While for HTTP/2 and above, CONNECT request may have a path, in general and for HTTP/1.1 CONNECT requests do
not have a path, and can only be matched using a
:ref:`connect_matcher <envoy_v3_api_msg_config.route.v3.RouteMatch.ConnectMatcher>`. Please also note
that when doing non-wildcard domain matching for CONNECT requests, the CONNECT target is  matched
rather than the Host/Authority header. You may need to include the port (e.g. "hostname:port") to
successfully match.

Envoy can handle CONNECT in one of two ways, either proxying the CONNECT headers through as if they
were any other request, and letting the upstream terminate the CONNECT request, or by terminating the
CONNECT request, and forwarding the payload as raw TCP data. When CONNECT upgrade configuration is
set up, the default behavior is to proxy the CONNECT request, treating it like any other request using
the upgrade path.
If termination is desired, this can be accomplished by setting
:ref:`connect_config <envoy_v3_api_field_config.route.v3.RouteAction.UpgradeConfig.connect_config>`
If it that message is present for CONNECT requests, the router filter will strip the request headers,
and forward the HTTP payload upstream. On receipt of initial TCP data from upstream, the router
will synthesize 200 response headers, and then forward the TCP data as the HTTP response body.

.. warning::
  This mode of CONNECT support can create major security holes if not configured correctly, as the upstream
  will be forwarded *unsanitized* headers if they are in the body payload. Please use with caution

For an example of proxying connect, please see :repo:`configs/proxy_connect.yaml <configs/proxy_connect.yaml>`
For an example of terminating connect, please see :repo:`configs/terminate_http1_connect.yaml <configs/terminate_http1_connect.yaml>` and :repo:`configs/terminate_http2_connect.yaml <configs/terminate_http2_connect.yaml>`

Note that for CONNECT-over-tls, Envoy can not currently be configured to do the CONNECT request in the clear
and encrypt previously unencrypted payload in one hop. To send CONNECT in plaintext and encrypt the payload,
one must first forward the HTTP payload over an "upstream" TLS loopback connection to encrypt it, then have
a TCP listener take the encrypted payload and send the CONNECT upstream.

.. _tunneling-tcp-over-http:

Tunneling TCP over HTTP
^^^^^^^^^^^^^^^^^^^^^^^
Envoy also has support for tunneling raw TCP over HTTP CONNECT or HTTP POST requests. Find
below some usage scenarios.

HTTP/2+ CONNECT can be used to proxy multiplexed TCP over pre-warmed secure connections and amortize
the cost of any TLS handshake.
An example set up proxying SMTP would look something like this:

[SMTP Upstream] --- raw SMTP --- [L2 Envoy]  --- SMTP tunneled over HTTP/2 CONNECT --- [L1 Envoy]  --- raw SMTP  --- [Client]

HTTP/1.1 CONNECT can be used to have TCP client connecting to its own
destination passing through an HTTP proxy server (e.g. corporate proxy not
supporting HTTP/2):

[HTTP Server] --- raw HTTP --- [L2 Envoy]  --- HTTP tunneled over HTTP/1.1 CONNECT --- [L1 Envoy]  --- raw HTTP  --- [HTTP Client]

Note that when using HTTP/1 CONNECT you will end up having a TCP connection
between L1 and L2 Envoy for each TCP client connection, it is preferable to use
HTTP/2 or above when you have the choice.

HTTP POST can also be used to proxy multiplexed TCP when intermediate proxies that don't support
CONNECT. An example set up proxying HTTP would look something like this:

[TCP Server] --- raw TCP --- [L2 Envoy]  --- TCP tunneled over HTTP/2 or HTTP/1.1 POST --- [Intermidate Proxies] --- HTTP/2 or HTTP/1.1 POST --- [L1 Envoy]  --- raw TCP  --- [TCP Client]

Examples of such a set up can be found in the Envoy example config :repo:`directory <configs/>`
For HTTP/1.1 CONNECT run ``bazel-bin/source/exe/envoy-static --config-path configs/encapsulate_in_http1_connect.yaml --base-id 1``
and ``bazel-bin/source/exe/envoy-static --config-path configs/terminate_http1_connect.yaml``.
For HTTP/2 CONNECT run ``bazel-bin/source/exe/envoy-static --config-path configs/encapsulate_in_http2_connect.yaml --base-id 1``
and ``bazel-bin/source/exe/envoy-static --config-path configs/terminate_http2_connect.yaml``.
For HTTP/2 POST run ``bazel-bin/source/exe/envoy-static --config-path configs/encapsulate_in_http2_post.yaml --base-id 1``
and ``bazel-bin/source/exe/envoy-static --config-path configs/terminate_http2_post.yaml``.

In all cases you will be running a first Envoy listening for TCP traffic on port 10000 and
encapsulating it in an HTTP CONNECT or HTTP POST request, and a second one listening on 10001,
stripping the CONNECT headers (not needed for POST request), and forwarding the original TCP
upstream, in this case to google.com.

Envoy waits for the HTTP tunnel to be established (i.e. a successful response to the  CONNECT request is received),
before start streaming the downstream TCP data to the upstream.

If you want to decapsulate a CONNECT request and also do HTTP processing on the decapsulated payload, the easiest way
to accomplish it is to use :ref:`internal listeners <config_internal_listener>`.
