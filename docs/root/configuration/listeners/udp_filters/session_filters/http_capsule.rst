.. _config_udp_session_filters_http_capsule:

HTTP Capsule filter
==================================

Tunneling UDP datagrams over HTTP (see `Proxying UDP in HTTP <https://www.rfc-editor.org/rfc/rfc9298.html>`_) requires an encapsulation mechanism to preserve the boundaries of the original datagram.
This filter applies the `HTTP Datagrams and the Capsule Protocol <https://www.rfc-editor.org/rfc/rfc9297.html>`_ to downstream and upstream datagrams, so they are compliant with the Capsule Protocol.

.. note::
  This filter must be used last in the UDP session filter chain, and should only be used when tunneling UDP over HTTP streams.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.http_capsule.v3.FilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.session.http_capsule.v3.FilterConfig>`
