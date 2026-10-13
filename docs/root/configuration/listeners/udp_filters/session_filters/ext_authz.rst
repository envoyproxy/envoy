.. _config_udp_session_filters_ext_authz:

External authorization
==================================

The external authorization session filter calls an external authorization service when a new UDP
session is established, to check whether the session is authorized. The session's downstream source
and destination addresses are sent as the ``source`` and ``destination`` peers of the request. If
the session is denied, it is dropped and no upstream is created.

The filter uses the gRPC Authorization API defined by
:ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>`. A failed check, or an error
when :ref:`failure_mode_allow
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.session.ext_authz.v3.FilterConfig.failure_mode_allow>`
is not set, causes the session to be dropped.

By default, datagrams that arrive while the authorization call is in flight are dropped. Configuring
:ref:`buffer_options
<envoy_v3_api_field_extensions.filters.udp.udp_proxy.session.ext_authz.v3.FilterConfig.buffer_options>`
enables buffering of those datagrams, which are then replayed if the session is allowed.

.. note::

  This filter authorizes a session by its source and destination addresses only and does not inspect
  datagram contents. It is not intended to authorize DNS requests, since the DNS query is not made
  available to the authorization service.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.ext_authz.v3.FilterConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.udp.udp_proxy.session.ext_authz.v3.FilterConfig>`

Authorization request
---------------------

The :ref:`CheckRequest <envoy_v3_api_msg_service.auth.v3.CheckRequest>` is populated from the
session's addresses:

.. csv-table::
  :header: CheckRequest field, Value
  :widths: 1, 2

  ``attributes.source.address.socket_address``, Downstream source IP and port.
  ``attributes.destination.address.socket_address``, Local address the datagrams were received on.

Authorization response
----------------------

The session outcome is taken from the
:ref:`CheckResponse <envoy_v3_api_msg_service.auth.v3.CheckResponse>`:

* an ``OK`` status allows the session.
* a denied response (non-``OK`` status, no ``error_response``) drops the session.
* an error (a gRPC failure or timeout, or a response carrying an ``error_response``) drops the
  session unless :ref:`failure_mode_allow
  <envoy_v3_api_field_extensions.filters.udp.udp_proxy.session.ext_authz.v3.FilterConfig.failure_mode_allow>`
  is set.

.. _config_udp_session_filters_ext_authz_stats:

Statistics
----------

Every configured filter has statistics rooted at *udp.session.ext_authz.<stat_prefix>.*
with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  ok, Counter, Number of sessions allowed by the authorization service
  denied, Counter, Number of sessions denied by the authorization service
  error, Counter, Number of errors contacting the authorization service
  failure_mode_allowed, Counter, Number of sessions allowed on error due to failure_mode_allow
  total, Counter, Total number of authorization checks issued
  buffer_overflow, Counter, Number of datagrams dropped while waiting for the authorization response due to the buffer being full
  active, Gauge, Number of authorization checks currently in flight
