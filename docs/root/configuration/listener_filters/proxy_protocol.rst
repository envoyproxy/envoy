.. _config_listener_filters_proxy_protocol:

Proxy Protocol
==============

This listener filter adds support for
`HAProxy Proxy Protocol <http://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_.

In this mode, the upstream connection is assumed to come from a proxy
which places the original coordinates (IP, PORT) into a connection-string.
Envoy then extracts these and uses them as the remote address.

In Proxy Protocol v2 there exists the concept of extensions (TLV)
tags that are optional. This implementation skips over these without
using them.

This implementation supports both version 1 and version 2, it
automatically determines on a per-connection basis which of the two
versions is present. Note: if the filter is enabled, the Proxy Protocol
must be present on the connection (either version 1 or version 2),
the standard does not allow parsing to determine if it is present or not.

If there is a protocol error or an unsupported address family
(e.g. AF_UNIX) the connection will be closed and an error thrown.

* :ref:`v2 API reference <envoy_api_field_listener.Filter.name>`
* This filter should be configured with the name *envoy.listener.proxy_protocol*.
