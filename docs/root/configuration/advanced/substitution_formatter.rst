.. substitution_formatter:

Substitution Formatter
======================

The substitution formatter allows you to define custom substitutions that can be used
in various configuration fields throughout Envoy. This feature enables dynamic content generation
based on runtime data, making configurations more flexible and adaptable to different environments.

For example, you can define a substitution that retrieves the current timestamp or the value of an
environment variable, and use it in logging formats, headers, or other configuration parameters.

.. The substitution formatter is used for access logging initially. So there are lots of labels
   containing "access_log" in the following sections. We keep the labels unchanged for backward
   compatibility, even though the substitution formatter can be used in other places beyond
   access logging.


.. _config_advanced_substitution_operators:

Supported commands
------------------

Current supported substitution commands include:

.. _config_access_log_format_start_time:

``%START_TIME%``
  HTTP/THRIFT
    Request start time including milliseconds.

  TCP
    Downstream connection start time including milliseconds.

  UDP
    UDP proxy session start time including milliseconds.

  ``START_TIME`` can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  In addition, ``START_TIME`` also accepts the following specifiers:

  +------------------------+-------------------------------------------------------------+
  | Specifier              | Explanation                                                 |
  +========================+=============================================================+
  | ``%s``                 | The number of seconds since the Epoch                       |
  +------------------------+-------------------------------------------------------------+
  | ``%f``, ``%[1-9]f``    | Fractional seconds digits, default is 9 digits (nanosecond) |
  |                        +-------------------------------------------------------------+
  |                        | - ``%3f`` millisecond (3 digits)                            |
  |                        | - ``%6f`` microsecond (6 digits)                            |
  |                        | - ``%9f`` nanosecond (9 digits)                             |
  +------------------------+-------------------------------------------------------------+

  Examples of formatting ``START_TIME`` are as follows:

  .. code-block:: none

    %START_TIME(%Y/%m/%dT%H:%M:%S%z)%

    %START_TIME(%s)%

    # To include millisecond fraction of the second (.000 ... .999). E.g. 1527590590.528.
    %START_TIME(%s.%3f)%

    %START_TIME(%s.%6f)%

    %START_TIME(%s.%9f)%

  In typed JSON logs, ``START_TIME`` is always rendered as a string.

.. _config_access_log_format_start_time_local:

``%START_TIME_LOCAL%``
  Same as :ref:`START_TIME <config_access_log_format_start_time>`, but use local time zone.

.. _config_access_log_format_emit_time:

``%EMIT_TIME%``
  The time when log entry is emitted including milliseconds.

  ``EMIT_TIME`` can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

.. _config_access_log_format_emit_time_local:

``%EMIT_TIME_LOCAL%``
  Same as :ref:`EMIT_TIME <config_access_log_format_emit_time>`, but use local time zone.

``%REQUEST_HEADERS_BYTES%``
  HTTP
    Uncompressed bytes of request headers.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%BYTES_RECEIVED%``
  HTTP/THRIFT
    Body bytes received.

  TCP
    Downstream bytes received on connection.

  UDP
    Bytes received from the downstream in the UDP session.

  Renders a numeric value in typed JSON logs.

``%BYTES_RETRANSMITTED%``
  HTTP/3 (QUIC)
    Body bytes retransmitted.

  HTTP/1 and HTTP/2
    Not implemented. It will appear as ``0`` in the access logs.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%PACKETS_RETRANSMITTED%``
  HTTP/3 (QUIC)
    Number of packets retransmitted.

  HTTP/1 and HTTP/2
    Not implemented. It will appear as ``0`` in the access logs.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%PROTOCOL%``
  HTTP
    Protocol. Currently either **HTTP/1.1**, **HTTP/2** or **HTTP/3**.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  In typed JSON logs, ``PROTOCOL`` will render the string ``"-"`` if the protocol is not
  available (e.g., in TCP logs).

``%UPSTREAM_PROTOCOL%``
  HTTP
    Upstream protocol. Currently either **HTTP/1.1**, **HTTP/2** or **HTTP/3**.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  In typed JSON logs, ``UPSTREAM_PROTOCOL`` will render the string ``"-"`` if the protocol is not
  available (e.g., in TCP logs).

``%RESPONSE_CODE%``
  HTTP
    HTTP response code.

    .. note::

      A response code of ``0`` means that the server never sent the beginning of a response.
      This generally means that the (downstream) client disconnected.

    .. note::

      In the case of ``100``-continue responses, only the response code of the final headers
      will be logged. If a ``100``-continue is followed by a ``200``, the logged response will be ``200``.
      If a ``100``-continue results in a disconnect, the ``100`` will be logged.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

.. _config_access_log_format_response_code_details:

``%RESPONSE_CODE_DETAILS(X)%``
  HTTP
    HTTP response code details provides additional information about the response code, such as
    who set it (the upstream or envoy) and why. The string will not contain any whitespaces, which
    will be converted to underscore '_', unless optional parameter ``X`` is ``ALLOW_WHITESPACES``.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_connection_termination_details:

``%CONNECTION_TERMINATION_DETAILS%``
  HTTP and TCP
    Connection termination details may provide additional information about why the connection was
    terminated by Envoy for L4 reasons.

``%RESPONSE_HEADERS_BYTES%``
  HTTP
    Uncompressed bytes of response headers.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%RESPONSE_TRAILERS_BYTES%``
  HTTP
    Uncompressed bytes of response trailers.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%BYTES_SENT%``
  HTTP/THRIFT
    Body bytes sent. For WebSocket connection it will also include response header bytes.

  TCP
    Downstream bytes sent on connection.

  UDP
    Bytes sent to the downstream in the UDP session.

``%UPSTREAM_REQUEST_ATTEMPT_COUNT%``
  HTTP
    Number of times the request is attempted upstream.

    .. note::

      An attempt count of ``0`` means that the request was never attempted upstream.

  TCP
    Number of times the connection request is attempted upstream.

    .. note::

      An attempt count of ``0`` means that the connection request was never attempted upstream.

  UDP
    Not implemented. It will appear as ``0`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%UPSTREAM_WIRE_BYTES_SENT%``
  HTTP
    Total number of bytes sent to the upstream by the http stream.

  TCP
    Total number of bytes sent to the upstream by the tcp proxy.

  UDP
    Total number of bytes sent to the upstream stream, For UDP tunneling flows. Not supported for non-tunneling.

``%UPSTREAM_WIRE_BYTES_RECEIVED%``
  HTTP
    Total number of bytes received from the upstream by the http stream.

  TCP
    Total number of bytes received from the upstream by the tcp proxy.

  UDP
    Total number of bytes received from the upstream stream, For UDP tunneling flows. Not supported for non-tunneling.

``%UPSTREAM_HEADER_BYTES_SENT%``
  HTTP
    Number of header bytes sent to the upstream by the http stream.

  TCP
    Total number of HTTP header bytes sent to the upstream stream, for TCP tunneling flows. Not supported for non-tunneling.

  UDP
    Total number of HTTP header bytes sent to the upstream stream, For UDP tunneling flows. Not supported for non-tunneling.

``%UPSTREAM_DECOMPRESSED_HEADER_BYTES_SENT%``
  HTTP
    Number of decompressed header bytes sent to the upstream by the http stream.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%UPSTREAM_HEADER_BYTES_RECEIVED%``
  HTTP
    Number of header bytes received from the upstream by the http stream.

  TCP
    Total number of HTTP header bytes received from the upstream stream, for TCP tunneling flows. Not supported for non-tunneling.

  UDP
    Total number of HTTP header bytes received from the upstream stream, For UDP tunneling flows. Not supported for non-tunneling.

``%UPSTREAM_DECOMPRESSED_HEADER_BYTES_RECEIVED%``
  HTTP
    Number of decompressed header bytes received from the upstream by the http stream.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.


``%DOWNSTREAM_WIRE_BYTES_SENT%``
  HTTP
    Total number of bytes sent to the downstream by the http stream.

  TCP
    Total number of bytes sent to the downstream by the tcp proxy.

  UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%DOWNSTREAM_WIRE_BYTES_RECEIVED%``
  HTTP
    Total number of bytes received from the downstream by the http stream. Envoy over counts sizes of received HTTP/1.1 pipelined requests by adding up bytes of requests in the pipeline to the one currently being processed.

  TCP
    Total number of bytes received from the downstream by the tcp proxy.

  UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%DOWNSTREAM_HEADER_BYTES_SENT%``
  HTTP
    Number of header bytes sent to the downstream by the http stream.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%DOWNSTREAM_DECOMPRESSED_HEADER_BYTES_SENT%``
  HTTP
    Number of decompressed header bytes sent to the downstream by the http stream.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%DOWNSTREAM_HEADER_BYTES_RECEIVED%``
  HTTP
    Number of header bytes received from the downstream by the http stream.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%DOWNSTREAM_DECOMPRESSED_HEADER_BYTES_RECEIVED%``
  HTTP
    Number of decompressed header bytes received from the downstream by the http stream.

  TCP/UDP
    Not implemented. It will appear as ``0`` in the access logs.

.. _config_access_log_format_duration:

``%DURATION%``
  HTTP/THRIFT
    Total duration in milliseconds of the request from the start time to the last byte out.

  TCP
    Total duration in milliseconds of the downstream connection.

  UDP
    Not implemented. It will appear as ``0`` in the access logs.

  Renders a numeric value in typed JSON logs.

.. _config_access_log_format_common_duration:

``%COMMON_DURATION(START:END:PRECISION)%``
  HTTP
    Total duration between the ``START`` time point and the ``END`` time point in specific ``PRECISION``.
    The ``START`` and ``END`` time points are specified by the following values (all values
    here are case-sensitive):

    * ``DS_RX_BEG``: The time point of the downstream request receiving begin.
    * ``DS_RX_END``: The time point of the downstream request receiving end.
    * ``US_CX_BEG``: The time point of the upstream TCP connect begin.
    * ``US_CX_END``: The time point of the upstream TCP connect end.
    * ``US_HS_END``: The time point of the upstream TLS handshake end.
    * ``US_TX_BEG``: The time point of the upstream request sending begin.
    * ``US_TX_END``: The time point of the upstream request sending end.
    * ``US_RX_BEG``: The time point of the upstream response receiving begin.
    * ``US_RX_BODY_BEG``: The time point of the upstream response body receiving begin.
    * ``US_RX_END``: The time point of the upstream response receiving end.
    * ``DS_TX_BEG``: The time point of the downstream response sending begin.
    * ``DS_TX_END``: The time point of the downstream response sending end.
    * Dynamic value: Other values will be treated as custom time points that are set by named keys.

    .. note::

      Upstream connection establishment time points (``US_CX_*``, ``US_HS_END``) repeat for all requests
      in a given connection.

    The ``PRECISION`` is specified by the following values (all values here are case-sensitive):

    * ``ms``: Millisecond precision.
    * ``us``: Microsecond precision.
    * ``ns``: Nanosecond precision.

    .. note::

      Enabling independent half-close behavior for H/2 and H/3 protocols can produce
      ``*_TX_END`` values lower than ``*_RX_END`` values, in cases where upstream peer has half-closed
      its stream before downstream peer. In these cases the ``COMMON_DURATION`` value will become negative.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%REQUEST_DURATION%``
  HTTP
    Total duration in milliseconds of the request from the start time to the last byte of
    the request received from the downstream.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%REQUEST_TX_DURATION%``
  HTTP
    Total duration in milliseconds of the request from the start time to the last byte sent upstream.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%RESPONSE_DURATION%``
  HTTP
    Total duration in milliseconds of the request from the start time to the first byte read from the
    upstream host.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%ROUNDTRIP_DURATION%``
  HTTP/3 (QUIC)
    Total duration in milliseconds of the request from the start time to receiving the final ack from
    the downstream.

  HTTP/1 and HTTP/2
    Not implemented. It will appear as ``"-"`` in the access logs.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%RESPONSE_TX_DURATION%``
  HTTP
    Total duration in milliseconds of the request from the first byte read from the upstream host to the last
    byte sent downstream.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%DOWNSTREAM_HANDSHAKE_DURATION%``
  HTTP
    Not implemented. It will appear as ``"-"`` in the access logs.

  TCP
    Total duration in milliseconds from the start of the connection to the TLS handshake being completed.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

``%UPSTREAM_CONNECTION_POOL_READY_DURATION%``
  HTTP/TCP
    Total duration in milliseconds from when the upstream request was created to when the connection pool is ready.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  Renders a numeric value in typed JSON logs.

.. _config_access_log_format_response_flags:

``%RESPONSE_FLAGS%`` / ``%RESPONSE_FLAGS_LONG%``
  Additional details about the response or connection, if any. For TCP connections, the response codes mentioned in
  the descriptions do not apply. ``%RESPONSE_FLAGS%`` will output a short string. ``%RESPONSE_FLAGS_LONG%`` will output a Pascal case string.
  Possible values are:

  HTTP and TCP

  .. csv-table::
    :header: Long name, Short name, Description
    :widths: 1, 1, 3

    ``NoHealthyUpstream``, ``UH``, No healthy upstream hosts in upstream cluster in addition to ``503`` response code.
    ``UpstreamConnectionFailure``, ``UF``, Upstream connection failure in addition to ``503`` response code.
    ``UpstreamOverflow``, ``UO``, Upstream overflow (:ref:`circuit breaking <arch_overview_circuit_break>`) in addition to ``503`` response code.
    ``NoRouteFound``, ``NR``, No :ref:`route configured <arch_overview_http_routing>` for a given request in addition to ``404`` response code or no matching filter chain for a downstream connection.
    ``UpstreamRetryLimitExceeded``, ``URX``, The request was rejected because the :ref:`upstream retry limit (HTTP) <envoy_v3_api_field_config.route.v3.RetryPolicy.num_retries>`  or :ref:`maximum connect attempts (TCP) <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_connect_attempts>` was reached.
    ``NoClusterFound``, ``NC``, Upstream cluster not found.
    ``DurationTimeout``, ``DT``, When a request or connection exceeded :ref:`max_connection_duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration>` or :ref:`max_downstream_connection_duration <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_downstream_connection_duration>`.

  HTTP only

  .. csv-table::
    :header: Long name, Short name, Description
    :widths: 1, 1, 3

    ``DownstreamConnectionTermination``, ``DC``, Downstream connection termination.
    ``FailedLocalHealthCheck``, ``LH``, Local service failed :ref:`health check request <arch_overview_health_checking>` in addition to ``503`` response code.
    ``UpstreamRequestTimeout``, ``UT``, Upstream request timeout in addition to ``504`` response code.
    ``LocalReset``, ``LR``, Connection local reset in addition to ``503`` response code.
    ``UpstreamRemoteReset``, ``UR``, Upstream remote reset in addition to ``503`` response code.
    ``UpstreamConnectionTermination``, ``UC``, Upstream connection termination in addition to ``503`` response code.
    ``DelayInjected``, ``DI``, The request processing was delayed for a period specified via :ref:`fault injection <config_http_filters_fault_injection>`.
    ``FaultInjected``, ``FI``, The request was aborted with a response code specified via :ref:`fault injection <config_http_filters_fault_injection>`.
    ``RateLimited``, ``RL``, The request was rate-limited locally by the :ref:`HTTP rate limit filter <config_http_filters_rate_limit>` in addition to ``429`` response code.
    ``UnauthorizedExternalService``, ``UAEX``, The request was denied by the external authorization service.
    ``RateLimitServiceError``, ``RLSE``, The request was rejected because there was an error in rate limit service.
    ``InvalidEnvoyRequestHeaders``, ``IH``, The request was rejected because it set an invalid value for a :ref:`strictly-checked header <envoy_v3_api_field_extensions.filters.http.router.v3.Router.strict_check_headers>` in addition to ``400`` response code.
    ``StreamIdleTimeout``, ``SI``, Stream idle timeout in addition to ``408`` or ``504`` response code.
    ``DownstreamProtocolError``, ``DPE``, The downstream request had an HTTP protocol error.
    ``UpstreamProtocolError``, ``UPE``, The upstream response had an HTTP protocol error.
    ``UpstreamMaxStreamDurationReached``, ``UMSDR``, The upstream request reached max stream duration.
    ``ResponseFromCacheFilter``, ``RFCF``, The response was served from an Envoy cache filter.
    ``NoFilterConfigFound``, ``NFCF``, The request is terminated because filter configuration was not received within the permitted warming deadline.
    ``OverloadManagerTerminated``, ``OM``, Overload Manager terminated the request.
    ``DnsResolutionFailed``, ``DF``, The request was terminated due to DNS resolution failure.
    ``DropOverload``, ``DO``, The request was terminated in addition to ``503`` response code due to :ref:`drop_overloads<envoy_v3_api_field_config.endpoint.v3.ClusterLoadAssignment.Policy.drop_overloads>`.
    ``DownstreamRemoteReset``, ``DR``, The response details are ``http2.remote_reset`` or ``http2.remote_refuse``.
    ``UnconditionalDropOverload``, ``UDO``, The request was terminated in addition to ``503`` response code due to :ref:`drop_overloads<envoy_v3_api_field_config.endpoint.v3.ClusterLoadAssignment.Policy.drop_overloads>` is set to ``100%``.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%ROUTE_NAME%``
  HTTP/TCP
    Name of the route.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%VIRTUAL_CLUSTER_NAME%``
  HTTP*/gRPC
    Name of the matched Virtual Cluster (if any).

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_upstream_host:

``%UPSTREAM_HOST%``
  Main address of upstream host (e.g., ip:port for TCP connections).

.. _config_access_log_format_upstream_host_name:

``%UPSTREAM_HOST_NAME%``
  Upstream host name (e.g., DNS name). If no DNS name is available, the main address of the upstream host
  (e.g., ip:port for TCP connections) will be used.

.. _config_access_log_format_upstream_host_name_without_port:

``%UPSTREAM_HOST_NAME_WITHOUT_PORT%``
  Upstream host name (e.g., DNS name) without port component. If no DNS name is available,
  the main address of the upstream host (e.g., ip for TCP connections) will be used.

``%UPSTREAM_CLUSTER%``
  Upstream cluster to which the upstream host belongs to. :ref:`alt_stat_name
  <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` will be used if provided.

``%UPSTREAM_CLUSTER_RAW%``
  Upstream cluster to which the upstream host belongs to. :ref:`alt_stat_name
  <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` does NOT modify this value.

``%UPSTREAM_LOCAL_ADDRESS%``
  Local address of the upstream connection. If the address is an IP address, it includes both
  address and port.

``%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT(MASK_PREFIX_LEN)%``
  Local address of the upstream connection, without any port component.
  IP addresses are the only address type with a port component.

  - If ``MASK_PREFIX_LEN`` is specified, the IP address is masked to that many bits and returned in CIDR notation.
  - If ``MASK_PREFIX_LEN`` is omitted, the unmasked address is returned (without port).
  - For IPv4, ``MASK_PREFIX_LEN`` must be between 0-32.
  - For IPv6, ``MASK_PREFIX_LEN`` must be between 0-128.

  Examples:

  - ``%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT(16)%`` returns ``10.1.0.0/16`` for source IP ``10.1.10.23``
  - ``%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT(64)%`` returns ``2001:db8:1234:5678::/64`` for source IP ``2001:db8:1234:5678:9abc:def0:1234:5678``
  - ``%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%`` returns ``10.1.10.23`` for source IP ``10.1.10.23``

``%UPSTREAM_LOCAL_PORT%``
  Local port of the upstream connection.
  IP addresses are the only address type with a port component.

.. _config_access_log_format_upstream_remote_address:

``%UPSTREAM_REMOTE_ADDRESS%``
  Remote address of the upstream connection. If the address is an IP address, it includes both
  address and port. Identical to the :ref:`UPSTREAM_HOST <config_access_log_format_upstream_host>` value if the upstream
  host only has one address and connection is established successfully.

``%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT(MASK_PREFIX_LEN)%``
  Remote address of the upstream connection, without any port component.
  IP addresses are the only address type with a port component.

  - If ``MASK_PREFIX_LEN`` is specified, the IP address is masked to that many bits and returned in CIDR notation.
  - If ``MASK_PREFIX_LEN`` is omitted, the unmasked address is returned (without port).
  - For IPv4, ``MASK_PREFIX_LEN`` must be between 0-32.
  - For IPv6, ``MASK_PREFIX_LEN`` must be between 0-128.

  Examples:

  - ``%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT(16)%`` returns ``10.1.0.0/16`` for upstream IP ``10.1.10.23``
  - ``%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT(64)%`` returns ``2001:db8:1234:5678::/64`` for upstream IP ``2001:db8:1234:5678:9abc:def0:1234:5678``
  - ``%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%`` returns ``10.1.10.23`` for upstream IP ``10.1.10.23``

``%UPSTREAM_REMOTE_PORT%``
  Remote port of the upstream connection.
  IP addresses are the only address type with a port component.

``%UPSTREAM_REMOTE_ADDRESS_ENDPOINT_ID%``
  The endpoint ID of the Envoy internal address used to establish an upstream connection through an
  :ref:`internal listener <config_internal_listener>`. Envoy internal addresses are the only address
  type with an endpoint ID component.

.. _config_access_log_format_upstream_transport_failure_reason:

``%UPSTREAM_TRANSPORT_FAILURE_REASON%``
  HTTP
    If upstream connection failed due to transport socket (e.g., TLS handshake), provides the failure
    reason from the transport socket. The format of this field depends on the configured upstream
    transport socket. Common TLS failures are in :ref:`TLS troubleshooting <arch_overview_ssl_trouble_shooting>`.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_downstream_transport_failure_reason:

``%DOWNSTREAM_TRANSPORT_FAILURE_REASON%``
  HTTP/TCP
    If downstream connection failed due to transport socket (e.g., TLS handshake), provides the failure
    reason from the transport socket. The format of this field depends on the configured downstream
    transport socket. Common TLS failures are in :ref:`TLS troubleshooting <arch_overview_ssl_trouble_shooting>`.

    .. note::
      It only works in listener access config, and the HTTP or TCP access logs would observe empty values.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_downstream_local_close_reason:

``%DOWNSTREAM_LOCAL_CLOSE_REASON%``
  HTTP/TCP
    If downstream connection was closed locally, provides the reason.

  UDP
    Not implemented ("-")

.. _config_access_log_format_downstream_detected_close_type:

``%DOWNSTREAM_DETECTED_CLOSE_TYPE%``
  HTTP/TCP
    The detected close type of the downstream connection. This is only available on access logs recorded after the connection has been closed.
    Possible values are ``Normal``, ``LocalReset``, and ``RemoteReset``.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_REMOTE_ADDRESS%``
  Remote address of the downstream connection. If the address is an IP address, it includes both
  address and port.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

``%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT(MASK_PREFIX_LEN)%``
  Remote address of the downstream connection, without any port component.
  IP addresses are the only address type with a port component.

  - If ``MASK_PREFIX_LEN`` is specified, the IP address is masked to that many bits and returned in CIDR notation.
  - If ``MASK_PREFIX_LEN`` is omitted, the unmasked address is returned (without port).
  - For IPv4, ``MASK_PREFIX_LEN`` must be between 0-32.
  - For IPv6, ``MASK_PREFIX_LEN`` must be between 0-128.

  Examples:

  - ``%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT(16)%`` returns ``10.1.0.0/16`` for client IP ``10.1.10.23``
  - ``%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT(64)%`` returns ``2001:db8:1234:5678::/64`` for client IP ``2001:db8:1234:5678:9abc:def0:1234:5678``
  - ``%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%`` returns ``10.1.10.23`` for client IP ``10.1.10.23``

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

``%DOWNSTREAM_REMOTE_PORT%``
  Remote port of the downstream connection.
  IP addresses are the only address type with a port component.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

``%DOWNSTREAM_DIRECT_REMOTE_ADDRESS%``
  Direct remote address of the downstream connection. If the address is an IP address, it includes both
  address and port.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

``%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT(MASK_PREFIX_LEN)%``
  Direct remote address of the downstream connection, without any port component.
  IP addresses are the only address type with a port component.

  - If ``MASK_PREFIX_LEN`` is specified, the IP address is masked to that many bits and returned in CIDR notation.
  - If ``MASK_PREFIX_LEN`` is omitted, the unmasked address is returned (without port).
  - For IPv4, ``MASK_PREFIX_LEN`` must be between 0-32.
  - For IPv6, ``MASK_PREFIX_LEN`` must be between 0-128.

  Examples:

  - ``%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT(16)%`` returns ``10.1.0.0/16`` for client IP ``10.1.10.23``
  - ``%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT(64)%`` returns ``2001:db8:1234:5678::/64`` for client IP ``2001:db8:1234:5678:9abc:def0:1234:5678``
  - ``%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%`` returns ``10.1.10.23`` for client IP ``10.1.10.23``

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

``%DOWNSTREAM_DIRECT_REMOTE_PORT%``
  Direct remote port of the downstream connection.
  IP addresses are the only address type with a port component.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

``%DOWNSTREAM_LOCAL_ADDRESS%``
  Local address of the downstream connection. If the address is an IP address, it includes both
  address and port.

  If the original connection was redirected by iptables REDIRECT, this represents
  the original destination address restored by the
  :ref:`Original Destination Filter <config_listener_filters_original_dst>` using SO_ORIGINAL_DST socket option.
  If the original connection was redirected by iptables TPROXY, and the listener's transparent
  option was set to true, this represents the original destination address and port.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_DIRECT_LOCAL_ADDRESS%``
  Direct local address of the downstream connection.

  .. note::

    This is always the physical local address even if the downstream remote address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT(MASK_PREFIX_LEN)%``
  Local address of the downstream connection, without any port component.
  IP addresses are the only address type with a port component.

  - If ``MASK_PREFIX_LEN`` is specified, the IP address is masked to that many bits and returned in CIDR notation.
  - If ``MASK_PREFIX_LEN`` is omitted, the unmasked address is returned (without port).
  - For IPv4, ``MASK_PREFIX_LEN`` must be between 0-32.
  - For IPv6, ``MASK_PREFIX_LEN`` must be between 0-128.

  Examples:

  - ``%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT(16)%`` returns ``10.1.0.0/16`` for local IP ``10.1.10.23``
  - ``%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT(64)%`` returns ``2001:db8:1234:5678::/64`` for local IP ``2001:db8:1234:5678:9abc:def0:1234:5678``
  - ``%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%`` returns ``10.1.10.23`` for local IP ``10.1.10.23``

  .. note::

    This may not be the physical local address if the downstream local address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_DIRECT_LOCAL_ADDRESS_WITHOUT_PORT(MASK_PREFIX_LEN)%``
  Direct local address of the downstream connection, without any port component.

  - If ``MASK_PREFIX_LEN`` is specified, the IP address is masked to that many bits and returned in CIDR notation.
  - If ``MASK_PREFIX_LEN`` is omitted, the unmasked address is returned (without port).
  - For IPv4, ``MASK_PREFIX_LEN`` must be between 0-32.
  - For IPv6, ``MASK_PREFIX_LEN`` must be between 0-128.

  Examples:

  - ``%DOWNSTREAM_DIRECT_LOCAL_ADDRESS_WITHOUT_PORT(16)%`` returns ``10.1.0.0/16`` for local IP ``10.1.10.23``
  - ``%DOWNSTREAM_DIRECT_LOCAL_ADDRESS_WITHOUT_PORT(64)%`` returns ``2001:db8:1234:5678::/64`` for local IP ``2001:db8:1234:5678:9abc:def0:1234:5678``
  - ``%DOWNSTREAM_DIRECT_LOCAL_ADDRESS_WITHOUT_PORT%`` returns ``10.1.10.23`` for local IP ``10.1.10.23``

  .. note::

    This is always the physical local address even if the downstream local address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_LOCAL_PORT%``
  Local port of the downstream connection.
  IP addresses are the only address type with a port component.

  .. note::

    This may not be the physical port if the downstream local address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_DIRECT_LOCAL_PORT%``
  Direct local port of the downstream connection.
  IP addresses are the only address type with a port component.

  .. note::

    This is always the listener port even if the downstream local address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_LOCAL_ADDRESS_ENDPOINT_ID%``
  The endpoint ID of the local Envoy internal address on a downstream connection through an
  :ref:`internal listener <config_internal_listener>`. Envoy internal addresses are the only address
  type with an endpoint ID component.

  .. note::

    This may not be the endpoint ID if the downstream local address has been inferred from the
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

``%DOWNSTREAM_DIRECT_LOCAL_ADDRESS_ENDPOINT_ID%``
  The endpoint ID of the direct local Envoy internal address on a downstream connection through an
  :ref:`internal listener <config_internal_listener>`. Envoy internal addresses are the only address
  type with an endpoint ID component.

  .. note::

    This is always the endpoint ID even if the downstream local address has been inferred from the
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`.

.. _config_access_log_format_connection_id:

``%CONNECTION_ID%``
  An identifier for the downstream connection. It can be used to
  cross-reference TCP access logs across multiple log sinks, or to
  cross-reference timer-based reports for the same connection. The identifier
  is unique with high likelihood within an execution, but can duplicate across
  multiple instances or between restarts.

.. _config_access_log_format_upstream_connection_id:

``%UPSTREAM_CONNECTION_ID%``
  An identifier for the upstream connection. It can be used to
  cross-reference TCP access logs across multiple log sinks, or to
  cross-reference timer-based reports for the same connection. The identifier
  is unique with high likelihood within an execution, but can duplicate across
  multiple instances or between restarts.

.. _config_access_log_format_stream_id:

``%STREAM_ID%``
  An identifier for the stream (HTTP request, long-live HTTP2 stream, TCP connection, etc.). It can be used to
  cross-reference TCP access logs across multiple log sinks, or to cross-reference timer-based reports for the same connection.
  Unlike ``%CONNECTION_ID%``, the identifier should be unique across multiple instances or between restarts.
  And its value should be the same as ``%REQUEST_HEADER(X-REQUEST-ID)%`` for HTTP requests.
  This should be used to replace ``%CONNECTION_ID%`` and ``%REQUEST_HEADER(X-REQUEST-ID)%`` in most cases.

``%GRPC_STATUS(X)%``
  `gRPC status code <https://github.com/googleapis/googleapis/blob/master/google/rpc/code.proto>`_ formatted according to the optional parameter ``X``, which can be ``CAMEL_STRING``, ``SNAKE_STRING`` and ``NUMBER``.
  For example, if the grpc status is ``INVALID_ARGUMENT`` (represented by number 3), the formatter will return ``InvalidArgument`` for ``CAMEL_STRING``, ``INVALID_ARGUMENT`` for ``SNAKE_STRING`` and ``3`` for ``NUMBER``.
  If ``X`` isn't provided, ``CAMEL_STRING`` will be used.

``%GRPC_STATUS_NUMBER%``
  gRPC status code.

.. _config_access_log_format_req:

``%REQUEST_HEADER(X?Y):Z%`` / ``%REQ(X?Y):Z%``
  HTTP
    An HTTP request header where ``X`` is the main HTTP header, ``Y`` is the alternative one, and ``Z`` is an
    optional parameter denoting string truncation up to ``Z`` characters long. The value is taken from
    the HTTP request header named ``X`` first and if it's not set, then request header ``Y`` is used. If
    none of the headers are present ``"-"`` symbol will be in the log.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%RESPONSE_HEADER(X?Y):Z%`` / ``%RESP(X?Y):Z%``
  HTTP
    Same as ``%REQUEST_HEADER(X?Y):Z%`` but taken from HTTP response headers.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%RESPONSE_TRAILER(X?Y):Z%`` / ``%TRAILER(X?Y):Z%``
  HTTP
    Same as ``%REQUEST_HEADER(X?Y):Z%`` but taken from HTTP response trailers.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_dynamic_metadata:

``%DYNAMIC_METADATA(NAMESPACE:KEY*):Z%``
  HTTP
    :ref:`Dynamic Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where ``NAMESPACE`` is the filter namespace used when setting the metadata, ``KEY`` is an optional
    lookup key in the namespace with the option of specifying nested keys separated by ':', and ``Z`` is an
    optional parameter denoting string (and other non-structured value) truncation up to ``Z`` characters long.
    Dynamic Metadata can be set by filters using the :repo:`StreamInfo <envoy/stream_info/stream_info.h>` API:
    *setDynamicMetadata*. The data will be logged as a JSON string. For example, for the following dynamic metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * ``%DYNAMIC_METADATA(com.test.my_filter)%`` will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * ``%DYNAMIC_METADATA(com.test.my_filter:test_key)%`` will log: ``foo``
    * ``%DYNAMIC_METADATA(com.test.my_filter:test_object)%`` will log: ``{"inner_key": "bar"}``
    * ``%DYNAMIC_METADATA(com.test.my_filter:test_object:inner_key)%`` will log: ``bar``
    * ``%DYNAMIC_METADATA(com.unknown_filter)%`` will log: ``-``
    * ``%DYNAMIC_METADATA(com.test.my_filter:unknown_key)%`` will log: ``-``
    * ``%DYNAMIC_METADATA(com.test.my_filter:test_object):2%`` will log (no truncation for struct): ``{"inner_key": "bar"}``
    * ``%DYNAMIC_METADATA(com.test.my_filter:test_key):2%`` will log (truncation at 2 characters): ``fo``

  TCP
    Not implemented. It will appear as ``"-"`` in the access logs.

  UDP
    For :ref:`UDP Proxy <config_udp_listener_filters_udp_proxy>`,
    when ``NAMESPACE`` is set to "udp.proxy.session", the following optional ``KEY`` values are available:

    * ``cluster_name``: Name of the cluster.
    * ``bytes_sent``: Total number of bytes sent to the downstream in the session.

      .. deprecated:: 1.32.0

       Please use ``%BYTES_SENT%`` instead.

    * ``bytes_received``: Total number of bytes received from the downstream in the session.

      .. deprecated:: 1.32.0

       Please use ``%BYTES_RECEIVED%`` instead.

    * ``errors_sent``: Number of errors that have occurred when sending datagrams to the downstream in the session.
    * ``datagrams_sent``: Number of datagrams sent to the downstream in the session.
    * ``datagrams_received``: Number of datagrams received from the downstream in the session.

    Recommended session access log format for UDP proxy:

    .. code-block:: none

      [%START_TIME%] %DYNAMIC_METADATA(udp.proxy.session:cluster_name)%
      %DYNAMIC_METADATA(udp.proxy.session:bytes_sent)%
      %DYNAMIC_METADATA(udp.proxy.session:bytes_received)%
      %DYNAMIC_METADATA(udp.proxy.session:errors_sent)%
      %DYNAMIC_METADATA(udp.proxy.session:datagrams_sent)%
      %DYNAMIC_METADATA(udp.proxy.session:datagrams_received)%

    when ``NAMESPACE`` is set to "udp.proxy.proxy", the following optional ``KEY`` values are available:

    * ``bytes_sent``: Total number of bytes sent to the downstream in UDP proxy.

      .. deprecated:: 1.32.0

       Please use ``%BYTES_SENT%`` instead.

    * ``bytes_received``: Total number of bytes received from the downstream in UDP proxy.

      .. deprecated:: 1.32.0

       Please use ``%BYTES_RECEIVED%`` instead.

    * ``errors_sent``: Number of errors that have occurred when sending datagrams to the downstream in UDP proxy.
    * ``errors_received``: Number of errors that have occurred when receiving datagrams from the downstream in UDP proxy.
    * ``datagrams_sent``: Number of datagrams sent to the downstream in UDP proxy.
    * ``datagrams_received``: Number of datagrams received from the downstream in UDP proxy.
    * ``no_route``: Number of times that no upstream cluster found in UDP proxy.
    * ``session_total``: Total number of sessions in UDP proxy.
    * ``idle_timeout``: Number of times that sessions idle timeout occurred in UDP proxy.

    Recommended proxy access log format for UDP proxy:

    .. code-block:: none

      [%START_TIME%]
      %DYNAMIC_METADATA(udp.proxy.proxy:bytes_sent)%
      %DYNAMIC_METADATA(udp.proxy.proxy:bytes_received)%
      %DYNAMIC_METADATA(udp.proxy.proxy:errors_sent)%
      %DYNAMIC_METADATA(udp.proxy.proxy:errors_received)%
      %DYNAMIC_METADATA(udp.proxy.proxy:datagrams_sent)%
      %DYNAMIC_METADATA(udp.proxy.proxy:datagrams_received)%
      %DYNAMIC_METADATA(udp.proxy.proxy:session_total)%

  THRIFT
    For :ref:`Thrift Proxy <config_network_filters_thrift_proxy>`,
    ``NAMESPACE`` should be always set to "thrift.proxy", the following optional ``KEY`` values are available:

    * ``method``: Name of the method.
    * ``cluster_name``: Name of the cluster.
    * ``passthrough``: Passthrough support for the request and response.
    * ``request:transport_type``: The transport type of the request.
    * ``request:protocol_type``: The protocol type of the request.
    * ``request:message_type``: The message type of the request.
    * ``response:transport_type``: The transport type of the response.
    * ``response:protocol_type``: The protocol type of the response.
    * ``response:message_type``: The message type of the response.
    * ``response:reply_type``: The reply type of the response.

    Recommended access log format for Thrift proxy:

    .. code-block:: none

      [%START_TIME%] %DYNAMIC_METADATA(thrift.proxy:method)%
      %DYNAMIC_METADATA(thrift.proxy:cluster)%
      %DYNAMIC_METADATA(thrift.proxy:request:transport_type)%
      %DYNAMIC_METADATA(thrift.proxy:request:protocol_type)%
      %DYNAMIC_METADATA(thrift.proxy:request:message_type)%
      %DYNAMIC_METADATA(thrift.proxy:response:transport_type)%
      %DYNAMIC_METADATA(thrift.proxy:response:protocol_type)%
      %DYNAMIC_METADATA(thrift.proxy:response:message_type)%
      %DYNAMIC_METADATA(thrift.proxy:response:reply_type)%
      %BYTES_RECEIVED%
      %BYTES_SENT%
      %DURATION%
      %UPSTREAM_HOST%

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored.

  .. note::

   The ``DYNAMIC_METADATA`` command operator will be deprecated in the future in favor of :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` operator.

.. _config_access_log_format_cluster_metadata:

``%CLUSTER_METADATA(NAMESPACE:KEY*):Z%``
  HTTP
    :ref:`Upstream cluster Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where ``NAMESPACE`` is the filter namespace used when setting the metadata, ``KEY`` is an optional
    lookup key in the namespace with the option of specifying nested keys separated by ':',
    and ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long. The data
    will be logged as a JSON string. For example, for the following dynamic metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * ``%CLUSTER_METADATA(com.test.my_filter)%`` will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * ``%CLUSTER_METADATA(com.test.my_filter:test_key)%`` will log: ``foo``
    * ``%CLUSTER_METADATA(com.test.my_filter:test_object)%`` will log: ``{"inner_key": "bar"}``
    * ``%CLUSTER_METADATA(com.test.my_filter:test_object:inner_key)%`` will log: ``bar``
    * ``%CLUSTER_METADATA(com.unknown_filter)%`` will log: ``-``
    * ``%CLUSTER_METADATA(com.test.my_filter:unknown_key)%`` will log: ``-``
    * ``%CLUSTER_METADATA(com.test.my_filter):25%`` will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  TCP/UDP/THRIFT
    Not implemented. It will appear as ``"-"`` in the access logs.

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored.

  .. note::

   The ``CLUSTER_METADATA`` command operator will be deprecated in the future in favor of :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` operator.

.. _config_access_log_format_upstream_host_metadata:

``%UPSTREAM_METADATA(NAMESPACE:KEY*):Z%``
  HTTP/TCP
    :ref:`Upstream host Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where ``NAMESPACE`` is the filter namespace used when setting the metadata, ``KEY`` is an optional
    lookup key in the namespace with the option of specifying nested keys separated by ':',
    and ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long. The data
    will be logged as a JSON string. For example, for the following upstream host metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * ``%UPSTREAM_METADATA(com.test.my_filter)%`` will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * ``%UPSTREAM_METADATA(com.test.my_filter:test_key)%`` will log: ``foo``
    * ``%UPSTREAM_METADATA(com.test.my_filter:test_object)%`` will log: ``{"inner_key": "bar"}``
    * ``%UPSTREAM_METADATA(com.test.my_filter:test_object:inner_key)%`` will log: ``bar``
    * ``%UPSTREAM_METADATA(com.unknown_filter)%`` will log: ``-``
    * ``%UPSTREAM_METADATA(com.test.my_filter:unknown_key)%`` will log: ``-``
    * ``%UPSTREAM_METADATA(com.test.my_filter):25%`` will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  UDP/THRIFT
    Not implemented. It will appear as ``"-"`` in the access logs.

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored.

  .. note::

   The ``UPSTREAM_METADATA`` command operator will be deprecated in the future in favor of :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` operator.

.. _config_access_log_format_filter_state:

``%FILTER_STATE(KEY:F:FIELD?):Z%``
  HTTP
    :ref:`Filter State <arch_overview_data_sharing_between_filters>` info, where the ``KEY`` is required to
    look up the filter state object. The serialized proto will be logged as JSON string if possible.
    If the serialized proto is unknown to Envoy it will be logged as protobuf debug string.
    ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.
    ``F`` is an optional parameter used to indicate which method FilterState uses for serialization.
    If ``PLAIN`` is set, the filter state object will be serialized as an unstructured string.
    If ``TYPED`` is set or no ``F`` provided, the filter state object will be serialized as an JSON string.
    If ``F`` is set to ``FIELD``, the filter state object field with the name ``FIELD`` will be serialized.
    ``FIELD`` parameter should only be used with ``F`` set to ``FIELD``.

  TCP/UDP
    Same as HTTP, the filter state is from connection instead of a L7 request.

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored

``%UPSTREAM_FILTER_STATE(KEY:F:FIELD?):Z%``
  HTTP
    Extracts filter state from upstream components like cluster or transport socket extensions.

    :ref:`Filter State <arch_overview_data_sharing_between_filters>` info, where the ``KEY`` is required to
    look up the filter state object. The serialized proto will be logged as JSON string if possible.
    If the serialized proto is unknown to Envoy it will be logged as protobuf debug string.
    ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.
    ``F`` is an optional parameter used to indicate which method FilterState uses for serialization.
    If ``PLAIN`` is set, the filter state object will be serialized as an unstructured string.
    If ``TYPED`` is set or no ``F`` provided, the filter state object will be serialized as an JSON string.
    If ``F`` is set to ``FIELD``, the filter state object field with the name ``FIELD`` will be serialized.
    ``FIELD`` parameter should only be used with ``F`` set to ``FIELD``.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  .. note::

    The ``UPSTREAM_FILTER_STATE`` command operator is only available for :ref:`upstream_log <envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_log>`.

``%REQUESTED_SERVER_NAME(X:Y)%``
  HTTP/TCP/THRIFT
    String value set on ssl connection socket for Server Name Indication (SNI) or host header.
    The parameter ``X`` is used to specify whether the output should fallback to the host header when SNI is not set.
    The parameter ``Y`` is used to specify the source of the request host. Both ``X`` and ``Y`` are optional. ``Y`` makes no sense
    when ``X`` is set to ``SNI_ONLY``.

    The ``X`` parameter can be:

    * ``SNI_ONLY``: String value set on ssl connection socket for Server Name Indication (SNI), this is the default value of ``X``.
    * ``SNI_FIRST``: The output will retrieve from ``:authority`` or ``x-envoy-original-host`` header when SNI is not set.
    * ``HOST_FIRST``: The output will retrieve from ``:authority`` or ``x-envoy-original-host`` header.

    The ``Y`` parameter can be:

    * ``ORIG``: Get the request host from the ``x-envoy-original-host`` header.
    * ``HOST``: Get the request host from the ``:authority`` header.
    * ``ORIG_OR_HOST``: Get the request host from the ``x-envoy-original-host`` header if it is
      present, otherwise get it from the ``:authority`` header. If the ``Y`` is not present, ``ORIG_OR_HOST``
      will be used.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_LOCAL_IP_SAN%``
  HTTP/TCP/THRIFT
    The ip addresses present in the SAN of the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_IP_SAN%``
  HTTP/TCP/THRIFT
    The ip addresses present in the SAN of the peer certificate received from the downstream client to establish the
    TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_LOCAL_DNS_SAN%``
  HTTP/TCP/THRIFT
    The DNS names present in the SAN of the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_DNS_SAN%``
  HTTP/TCP/THRIFT
    The DNS names present in the SAN of the peer certificate received from the downstream client to establish the
    TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_LOCAL_URI_SAN%``
  HTTP/TCP/THRIFT
    The URIs present in the SAN of the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_URI_SAN%``
  HTTP/TCP/THRIFT
    The URIs present in the SAN of the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_LOCAL_EMAIL_SAN%``
  HTTP/TCP/THRIFT
    The emails present in the SAN of the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_EMAIL_SAN%``
  HTTP/TCP/THRIFT
    The emails present in the SAN of the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_LOCAL_OTHERNAME_SAN%``
  HTTP/TCP/THRIFT
    The OtherNames present in the SAN of the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_OTHERNAME_SAN%``
  HTTP/TCP/THRIFT
    The OtherNames present in the SAN of the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_LOCAL_SUBJECT%``
  HTTP/TCP/THRIFT
    The subject present in the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_SUBJECT%``
  HTTP/TCP/THRIFT
    The subject present in the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_ISSUER%``
  HTTP/TCP/THRIFT
    The issuer present in the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_TLS_SESSION_ID%``
  HTTP/TCP/THRIFT
    The session ID for the established downstream TLS connection.
  UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%DOWNSTREAM_TLS_CIPHER%``
  HTTP/TCP/THRIFT
    The OpenSSL name for the set of ciphers used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_TLS_VERSION%``
  HTTP/TCP/THRIFT
    The TLS version (e.g., ``TLSv1.2``, ``TLSv1.3``) used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_FINGERPRINT_256%``
  HTTP/TCP/THRIFT
    The hex-encoded SHA256 fingerprint of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_FINGERPRINT_1%``
  HTTP/TCP/THRIFT
    The hex-encoded SHA1 fingerprint of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_SERIAL%``
  HTTP/TCP/THRIFT
    The serial number of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_256%``
  HTTP/TCP/THRIFT
    The comma-separated hex-encoded SHA256 fingerprints of all client certificates used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_CHAIN_FINGERPRINTS_1%``
  HTTP/TCP/THRIFT
    The comma-separated hex-encoded SHA1 fingerprints of all client certificates used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_CHAIN_SERIALS%``
  HTTP/TCP/THRIFT
    The comma-separated serial numbers of all client certificates used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%DOWNSTREAM_PEER_CERT%``
  HTTP/TCP/THRIFT
    The client certificate in the URL-encoded PEM format used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%TLS_JA3_FINGERPRINT%``
  HTTP/TCP/Thrift
    The JA3 fingerprint (MD5 hash) of the TLS Client Hello message from the downstream connection.
    Provides a way to fingerprint TLS clients based on various Client Hello parameters like cipher suites,
    extensions, elliptic curves, etc. Will be ``"-"`` if TLS is not used or the handshake is incomplete.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%TLS_JA4_FINGERPRINT%``
  HTTP/TCP/THRIFT
    The JA4 fingerprint of the TLS Client Hello message from the downstream connection. JA4 is an advanced TLS client
    fingerprinting method that provides more granularity than JA3 by including the protocol version, cipher preference
    order, and ALPN (Application-Layer Protocol Negotiation) protocols. This enhanced fingerprinting facilitates
    improved threat hunting and security analysis.

    The JA4 fingerprint follows the format ``a_b_c``, where:

    - **a**: Represents the TLS protocol version and cipher preference order.
    - **b**: Encodes the list of cipher suites offered by the client.
    - **c**: Contains the ALPN protocols advertised by the client.

    This structured format allows for detailed analysis of client applications based on their TLS handshake
    characteristics. It enables the identification of specific applications, underlying TLS libraries, and even
    potential malicious activities by comparing fingerprints against known profiles.

    If TLS is not used or the handshake is incomplete, the value of ``%TLS_JA4_FINGERPRINT%`` will be ``"-"``.

  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_downstream_peer_cert_v_start:

``%DOWNSTREAM_PEER_CERT_V_START%``
  HTTP/TCP/THRIFT
    The validity start date of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  ``DOWNSTREAM_PEER_CERT_V_START`` can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

.. _config_access_log_format_downstream_peer_cert_v_end:

``%DOWNSTREAM_PEER_CERT_V_END%``
  HTTP/TCP/THRIFT
    The validity end date of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  ``DOWNSTREAM_PEER_CERT_V_END`` can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

``%UPSTREAM_PEER_SUBJECT%``
  HTTP/TCP/THRIFT
    The subject present in the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_PEER_ISSUER%``
  HTTP/TCP/THRIFT
    The issuer present in the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_TLS_SESSION_ID%``
  HTTP/TCP/THRIFT
    The session ID for the established upstream TLS connection.
  UDP
    Not implemented. It will appear as ``0`` in the access logs.

``%UPSTREAM_TLS_CIPHER%``
  HTTP/TCP/THRIFT
    The OpenSSL name for the set of ciphers used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_TLS_VERSION%``
  HTTP/TCP/THRIFT
    The TLS version (e.g., ``TLSv1.2``, ``TLSv1.3``) used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_PEER_CERT%``
  HTTP/TCP/THRIFT
    The server certificate in the URL-encoded PEM format used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

.. _config_access_log_format_upstream_peer_cert_v_start:

``%UPSTREAM_PEER_CERT_V_START%``
  HTTP/TCP/THRIFT
    The validity start date of the upstream server certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  ``UPSTREAM_PEER_CERT_V_START`` can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

.. _config_access_log_format_upstream_peer_cert_v_end:

``%UPSTREAM_PEER_CERT_V_END%``
  HTTP/TCP/THRIFT
    The validity end date of the upstream server certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

  ``UPSTREAM_PEER_CERT_V_END`` can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

``%UPSTREAM_PEER_URI_SAN%``
  HTTP/TCP/THRIFT
    The URIs present in the SAN of the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_PEER_DNS_SAN%``
  HTTP/TCP/THRIFT
    The DNS names present in the SAN of the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_PEER_IP_SAN%``
  HTTP/TCP/THRIFT
    The ip addresses present in the SAN of the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_LOCAL_URI_SAN%``
  HTTP/TCP/THRIFT
    The URIs present in the SAN of the local certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_LOCAL_DNS_SAN%``
  HTTP/TCP/THRIFT
    The DNS names present in the SAN of the local certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%UPSTREAM_LOCAL_IP_SAN%``
  HTTP/TCP/THRIFT
    The ip addresses present in the SAN of the local certificate used to establish the upstream TLS connection.
  UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%HOSTNAME%``
  The system hostname.

``%LOCAL_REPLY_BODY%``
  The body text for the requests rejected by the Envoy.

``%FILTER_CHAIN_NAME%``
  The :ref:`network filter chain name <envoy_v3_api_field_config.listener.v3.FilterChain.name>` of the downstream connection.

.. _config_access_log_format_access_log_type:

``%ACCESS_LOG_TYPE%``
  The type of the access log, which indicates when the access log was recorded. If a non-supported log (from the list below)
  uses this substitution string, then the value will be an empty string.

  * ``TcpUpstreamConnected`` - When TCP Proxy filter has successfully established an upstream connection.
  * ``TcpPeriodic`` - On any TCP Proxy filter periodic log record.
  * ``TcpConnectionEnd`` - When a TCP connection is ended on TCP Proxy filter.
  * ``DownstreamStart`` - When HTTP Connection Manager filter receives a new HTTP request.
  * ``DownstreamTunnelSuccessfullyEstablished`` - When the HTTP Connection Manager sends response headers indicating a successful HTTP tunnel.
  * ``DownstreamPeriodic`` - On any HTTP Connection Manager periodic log record.
  * ``DownstreamEnd`` - When an HTTP stream is ended on HTTP Connection Manager filter.
  * ``UpstreamPoolReady`` - When a new HTTP request is received by the HTTP Router filter.
  * ``UpstreamPeriodic`` - On any HTTP Router filter periodic log record.
  * ``UpstreamEnd`` - When an HTTP request is finished on the HTTP Router filter.
  * ``UdpTunnelUpstreamConnected`` - When UDP Proxy filter has successfully established an upstream connection.

    .. note::

      It is only relevant for UDP tunneling over HTTP.

  * ``UdpPeriodic`` - On any UDP Proxy filter periodic log record.
  * ``UdpSessionEnd`` - When a UDP session is ended on UDP Proxy filter.

``%UNIQUE_ID%``
   A unique identifier (UUID) that is generated dynamically.

``%ENVIRONMENT(X):Z%``
  Environment value of environment variable ``X``. If no valid environment variable ``X``, ``"-"`` symbol will be used.
  ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.

``%TRACE_ID%``
  HTTP
    The trace ID of the request. If the request does not have a trace ID, this will be an empty string.
  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%QUERY_PARAM(X):Z%``
  HTTP
    The value of the query parameter ``X``. If the query parameter ``X`` is not present, ``"-"`` symbol will be used.
    ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.
  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%PATH(X:Y):Z%``
  HTTP
    The value of the request path. The parameter ``X`` is used to specify whether the output contains
    the query or not. The parameter ``Y`` is used to specify the source of the request path. Both ``X`` and ``Y``
    are optional. And ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.

    The ``X`` parameter can be:

    * ``WQ``: The output will be the full request path which contains the query parameters. If the ``X``
      is not present, ``WQ`` will be used.
    * ``NQ``: The output will be the request path without the query parameters.

    The ``Y`` parameter can be:

    * ``ORIG``: Get the request path from the ``x-envoy-original-path`` header.
    * ``PATH``: Get the request path from the ``:path`` header.
    * ``ORIG_OR_PATH``: Get the request path from the ``x-envoy-original-path`` header if it is
      present, otherwise get it from the ``:path`` header. If the ``Y`` is not present, ``ORIG_OR_PATH``
      will be used.
  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%CUSTOM_FLAGS%``
  Custom flags set into the stream info. This could be used to log any custom event from the filters.
  Multiple flags are separated by comma.

.. _config_access_log_format_coalesce:

``%COALESCE(JSON_CONFIG):Z%``
  HTTP
    A higher-order formatter operator that evaluates multiple formatter operators in sequence and
    returns the first non-null, non-empty result. This is useful for implementing fallback behavior,
    such as using SNI when available but falling back to the ``:authority`` header when SNI is not set.

    The ``JSON_CONFIG`` parameter is a JSON object with an ``operators`` array. Each operator can be
    specified as either:

    * A string representing a simple command name that does not require a parameter.
    * An object with the following fields:

      * ``command`` (required): The command name (e.g., ``REQ``, ``REQUESTED_SERVER_NAME``).
      * ``param`` (optional): The command parameter (e.g., ``:authority`` for the ``REQ`` command).
      * ``max_length`` (optional): Maximum length for this operator's output.

    ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters for the final output.

    .. note::

      The JSON parameter cannot contain literal ``)`` characters as they would interfere with the
      command parser. If you need a ``)`` character in a string value, use the Unicode escape
      sequence ``\u0029``.

    **Example: SNI with fallback to authority header**

    .. code-block:: none

      %COALESCE({"operators": ["REQUESTED_SERVER_NAME", {"command": "REQ", "param": ":authority"}]})%

    This returns the Server Name Indication (SNI) if available, otherwise falls back to the
    ``:authority`` header.

    **Example: Cascade fallback with multiple headers**

    .. code-block:: none

      %COALESCE({"operators": ["REQUESTED_SERVER_NAME", {"command": "REQ", "param": ":authority"}, {"command": "REQ", "param": "x-envoy-original-host"}]})%

    This tries SNI first, then ``:authority``, then ``x-envoy-original-host``.

    **Example: With length truncation**

    .. code-block:: none

      %COALESCE({"operators": [{"command": "REQ", "param": ":authority"}]}):50%

    This returns the ``:authority`` header value truncated to 50 characters.

    **Supported Commands**

    The ``COALESCE`` operator supports any built-in formatter command.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.


``%METADATA(TYPE:NAMESPACE:KEY*):Z%``
  HTTP
    :ref:`Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where ``TYPE`` is the type of metadata, ``NAMESPACE`` is the filter namespace used when setting
    the metadata, ``KEY`` is an optional lookup key in the namespace with the option of specifying
    nested keys separated by ':', and ``Z`` is an optional parameter denoting string truncation up to
    ``Z`` characters long. The data will be logged as a JSON string.

    The ``TYPE`` parameter can be one of the following (case-sensitive):

    * ``DYNAMIC``: Dynamic metadata
    * ``CLUSTER``: Upstream cluster metadata
    * ``ROUTE``: Route metadata
    * ``UPSTREAM_HOST``: Upstream host metadata
    * ``LISTENER``: Listener metadata
    * ``LISTENER_FILTER_CHAIN``: Listener filter chain metadata
    * ``VIRTUAL_HOST``: Virtual host metadata

    For example, for the following ROUTE metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * ``%METADATA(ROUTE:com.test.my_filter)%`` will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * ``%METADATA(ROUTE:com.test.my_filter:test_key)%`` will log: ``foo``
    * ``%METADATA(ROUTE:com.test.my_filter:test_object)%`` will log: ``{"inner_key": "bar"}``
    * ``%METADATA(ROUTE:com.test.my_filter:test_object:inner_key)%`` will log: ``bar``
    * ``%METADATA(ROUTE:com.unknown_filter)%`` will log: ``-``
    * ``%METADATA(ROUTE:com.test.my_filter:unknown_key)%`` will log: ``-``
    * ``%METADATA(ROUTE:com.test.my_filter):25%`` will log (truncation at 25 characters): ``{"test_key": "foo", "test``

    .. note::

      For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
      when the referenced key is a simple value. If the referenced key is a struct or list value, a
      JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
      length is ignored.

    .. note::

      ``%METADATA(DYNAMIC:NAMESPACE:KEY):Z%`` is equivalent to ``%DYNAMIC_METADATA(NAMESPACE:KEY):Z%``

      ``%METADATA(CLUSTER:NAMESPACE:KEY):Z%`` is equivalent to ``%CLUSTER_METADATA(NAMESPACE:KEY):Z%``

      ``%METADATA(UPSTREAM_HOST:NAMESPACE:KEY):Z%`` is equivalent to ``%UPSTREAM_METADATA(NAMESPACE:KEY):Z%``

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%CEL(EXPRESSION):Z%``
  HTTP
    Evaluates a Common Expression Language (CEL) expression based on Envoy :ref:`attributes <arch_overview_attributes>`.
    Expression errors are rendered as ``"-"``. ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.

    Examples:

    .. code-block:: none

      %CEL(response.code)%
      %CEL(connection.mtls)%
      %CEL(request.headers['x-envoy-original-path']):10%
      %CEL(request.headers['x-log-mtls'] || request.url_path.contains('v1beta3'))%

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%TYPED_CEL(EXPRESSION):Z%``
  HTTP
    Evaluates a Common Expression Language (CEL) expression and emits values of non-string types
    (number, boolean, null) in non-text access log formats like JSON. Otherwise functions the same as ``%CEL%``.
    CEL types not native to JSON are coerced as follows:

    * Bytes are base64 encoded to produce a string.
    * Durations are stringified as a count of seconds (e.g., ``duration("1h30m")`` becomes ``"5400s"``).
    * Timestamps are formatted to UTC (e.g., ``timestamp("2023-08-26T12:39:00-07:00")`` becomes ``"2023-08-26T19:39:00+00:00"``).
    * Maps become objects, provided all keys can be coerced to strings and all values can coerce to JSON-representable types.
    * Lists become lists, provided all values can coerce to JSON-representable types.

    ``Z`` is an optional parameter denoting string truncation up to ``Z`` characters long.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.

``%REQ_WITHOUT_QUERY(X?Y):Z%``
  HTTP
    An HTTP request header where ``X`` is the main HTTP header, ``Y`` is the alternative one, and ``Z`` is an
    optional parameter denoting string truncation up to ``Z`` characters long. The value is taken from
    the HTTP request header named ``X`` first and if it's not set, then request header ``Y`` is used. If
    none of the headers are present ``"-"`` symbol will be in the log.

    .. warning::

      This operator is deprecated. Please use ``%PATH%`` instead.

  TCP/UDP
    Not implemented. It will appear as ``"-"`` in the access logs.
