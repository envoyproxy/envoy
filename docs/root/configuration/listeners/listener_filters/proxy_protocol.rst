.. _config_listener_filters_proxy_protocol:

Proxy Protocol
==============

This listener filter adds support for
`HAProxy Proxy Protocol <https://www.haproxy.org/download/1.9/doc/proxy-protocol.txt>`_.

In this mode, the downstream connection is assumed to come from a proxy
which places the original coordinates (IP, PORT) into a connection-string.
Envoy then extracts these and uses them as the remote address.

In Proxy Protocol v2 there exists the concept of extensions (TLV)
tags that are optional. If the type of the TLV is added to the filter's configuration,
the TLV will be emitted as dynamic metadata with user-specified key.

This implementation supports both version 1 and version 2, it
automatically determines on a per-connection basis which of the two
versions is present.

.. note::
  If the filter is enabled, the Proxy Protocol must be present on the connection (either version 1 or version 2).
  The standard does not allow parsing to determine if it is present or not. However, the filter can be configured
  to allow the connection to be accepted without the Proxy Protocol header (against the standard).
  See :ref:`allow_requests_without_proxy_protocol <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.allow_requests_without_proxy_protocol>`.

If there is a protocol error or an unsupported address family
(e.g. AF_UNIX) the connection will be closed and an error thrown.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol>`

Statistics
----------

This filter emits the following general statistics, rooted at *proxy_proto.[<stat_prefix>.]*

.. csv-table::
  :header: Name, Type, Description
  :widths: 4, 1, 8

  not_found_disallowed, Counter, "Total number of connections that don't contain the PROXY protocol header and are rejected."
  not_found_allowed, Counter, "Total number of connections that don't contain the PROXY protocol header, but are allowed due to :ref:`allow_requests_without_proxy_protocol <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.allow_requests_without_proxy_protocol>`."

The filter also emits the statistics rooted at *proxy_proto.[<stat_prefix>.]versions.<version>*
for each matched PROXY protocol version. Proxy protocol versions include ``v1`` and ``v2``.

.. csv-table::
  :header: Name, Type, Description
  :widths: 4, 1, 8

  found, Counter, "Total number of connections where the PROXY protocol header was found and parsed correctly."
  disallowed, Counter, "Total number of ``found`` connections that are rejected due to :ref:`disallowed_versions <envoy_v3_api_field_extensions.filters.listener.proxy_protocol.v3.ProxyProtocol.disallowed_versions>`."
  error, Counter, "Total number of connections where the PROXY protocol header was malformed (and the connection was rejected)."

The filter also emits the following legacy statistics, rooted at its own scope and **not** including the *stat_prefix*:

.. csv-table::
  :header: Name, Type, Description
  :widths: 4, 1, 8

  downstream_cx_proxy_proto_error, Counter, "Total number of connections with proxy protocol errors, i.e. ``v1.error``, ``v2.error``, and ``not_found_disallowed``."

.. attention::
  Prefer using the more-detailed non-legacy statistics above.
