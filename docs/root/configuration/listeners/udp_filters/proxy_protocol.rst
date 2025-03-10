.. _config_udp_listener_filters_proxy_protocol:

PROXY Protocol UDP Filter
====================

.. attention::

  PROXY protocol UDP filter is under active development and should be considered alpha and not production ready.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.udp.dns_filter.v3.ProxyProtocol``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.proxy_protocol.v3.ProxyProtocol>`

Overview
--------

The PROXY protocol UDP listener filter allows Envoy to restore
the original source and destination addresses of datagrams using
the addresses stored in the encapsulated PROXY protocol header.

This filter is not session-oriented, and works on a per-datagram basis.
Each UDP datagram's source and destination addresses are overwritten with
the addresses in the encapsulated PROXY header before being passed to the
next filter. The PROXY header is also removed from the payload. Any TLVs 
are ignored.
This filter only supports PROXY protocol V2, since PROXY protocol V1 does
not explicitly support UDP.
Datagrams without a valid PROXY V2 header are dropped. Datagrams that do
not specify `SOCK_DGRAM (0x2)` as the socket protocol are also dropped. 