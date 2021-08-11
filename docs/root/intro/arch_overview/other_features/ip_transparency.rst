.. _arch_overview_ip_transparency:

IP Transparency
===============

What is IP Transparency
-----------------------

As a proxy, Envoy is an IP endpoint: it has its own IP address, distinct from that of any downstream
requests. Consequently, when Envoy establishes connections to upstream hosts, the IP address of that
connection will be different from that of any proxied connections.

Sometimes the upstream server or network may need to know the original IP address of the connection,
called the *downstream remote address*, for many reasons. Some examples include:

* the IP address being used to form part of an identity,
* the IP address being used to enforce network policy, or
* the IP address being included in an audit.

Envoy supports multiple methods for providing the downstream remote address to the upstream host.
These techniques vary in complexity and applicability.

Envoy also supports
:ref:`extensions <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`
for detecting the original IP address. This might be useful if none of the techniques below is
applicable to your setup. Two available extensions are the :ref:`custom header
<envoy_v3_api_msg_extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig>`
extension and the :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>`
extension.

HTTP Headers
------------

HTTP headers may carry the original IP address of the request in the
:ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` header. The upstream server
can use this header to determine the downstream remote address. Envoy may also use this header to
choose the IP address used by the
:ref:`Original Src HTTP Filter <arch_overview_ip_transparency_original_src_http>`.

The HTTP header approach has a few downsides:

* It is only applicable to HTTP.
* It may not be supported by the upstream host.
* It requires careful configuration.

Proxy Protocol
--------------

`HAProxy Proxy Protocol <http://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_ defines a
protocol for communicating metadata about a connection over TCP, prior to the main TCP stream. This
metadata includes the source IP. Envoy supports consuming this information using
:ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`, which may be used to recover
the downstream remote address for propagation into an
:ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` header. It can also be used in
conjunction with the
:ref:`Original Src Listener Filter <arch_overview_ip_transparency_original_src_listener>`. Finally,
Envoy supports generating this header using the :ref:`Proxy Protocol Transport Socket <extension_envoy.transport_sockets.upstream_proxy_protocol>`.

Here is an example config for setting up the socket:

.. code-block:: yaml

    clusters:
    - name: service1
      connect_timeout: 0.25s
      type: strict_dns
      lb_policy: round_robin
      transport_socket:
        name: envoy.transport_sockets.upstream_proxy_protocol
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.proxy_protocol.v3.ProxyProtocolUpstreamTransport
          config:
            version: V1
          transport_socket:
            name: envoy.transport_sockets.raw_buffer
      ...

There are several things to consider if you plan to use this socket in conjunction with the
:ref:`HTTP connection manager <config_http_conn_man>`. There will be a performance hit as there will be no upstream connection
re-use among downstream clients. Every client that connects to Envoy will get a new connection to the upstream server.
This is due to the nature of proxy protocol being a connection based protocol. Downstream client info is only forwarded to the
upstream at the start of a connection before any other data has been sent (Note: this includes before a TLS handshake occurs).
If possible, using the :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` header should be preferred as Envoy
will be able to re-use upstream connections with this method. Due to the disconnect between Envoy's handling of downstream and upstream
connections, it is a good idea to enforce short :ref:`idle timeouts <faq_configuration_timeouts>` on upstream connections as
Envoy will not inherently close a corresponding upstream connection when a downstream connection is closed.

Some drawbacks to Proxy Protocol:

* It only supports TCP protocols.
* It requires upstream host support.

.. _arch_overview_ip_transparency_original_src_listener:

Original Source Listener Filter
-------------------------------

In controlled deployments, it may be possible to replicate the downstream remote address on the
upstream connection by using a
:ref:`Original Source listener filter <config_listener_filters_original_src>`. No metadata is added
to the upstream request or stream. Rather, the upstream connection itself will be established with
the downstream remote address as its source address. This filter will work with any upstream
protocol or host. However, it requires fairly complex configuration, and it may not be supported in
all deployments due to routing constraints.

Some drawbacks to the Original Source filter:

* It requires that Envoy have access to the downstream remote address.
* Its configuration is relatively complex.
* It may introduce a slight performance hit due to restrictions on connection pooling.
* Not supported on Windows.

.. _arch_overview_ip_transparency_original_src_http:

Original Source HTTP Filter
---------------------------

In controlled deployments, it may be possible to replicate the downstream remote address on the
upstream connection by using a
:ref:`Original Source HTTP filter <config_http_filters_original_src>`. This filter operates much like
the :ref:`Original Src Listener Filter <arch_overview_ip_transparency_original_src_listener>`. The
main difference is that it can infer the original source address from HTTP headers, which is important
for cases where a single downstream connection carries multiple HTTP requests from different original
source addresses. Deployments with a front proxy forwarding to sidecar proxies are examples where case
applies.

This filter will work with any upstream HTTP host. However, it requires fairly complex configuration,
and it may not be supported in all deployments due to routing constraints.

Some drawbacks to the Original Source filter:

* It requires that Envoy be properly configured to extract the downstream remote address from the
  :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>` header.
* Its configuration is relatively complex.
* It may introduce a slight performance hit due to restrictions on connection pooling.

.. note::

 This feature is not supported on Windows.
