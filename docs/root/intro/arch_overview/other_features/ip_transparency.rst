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
:ref:`Original Src Listener Filter <arch_overview_ip_transparency_original_src_listener>`.

Some drawbacks to Proxy Protocol:

* It only supports TCP protocols.
* It requires upstream host support.
* Envoy cannot yet send it to the upstream.

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
