.. _config_listener_filters_proxy_protocol:

Proxy Protocol
==============

This listener filter adds support for
`HAProxy Proxy Protocol <http://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_.

In this mode, the upstream connection is assumed to come from a proxy
which places the original coordinates (IP, PORT) into a connection-string.
Envoy then extracts these and uses them as the remote address.

In Proxy Protocol v2 there exists the concept of extensions (TLV) tags
that are optional. This implementation skips over these without using
them.

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`
