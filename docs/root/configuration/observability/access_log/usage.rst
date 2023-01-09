  .. _config_access_log:

Access logging
==============

Configuration
-------------------------

Access logs are configured as part of the :ref:`HTTP connection manager config
<config_http_conn_man>`, :ref:`TCP Proxy <config_network_filters_tcp_proxy>`,
:ref:`UDP Proxy <config_udp_listener_filters_udp_proxy>` or
:ref:`Thrift Proxy <config_network_filters_thrift_proxy>`.

* :ref:`v3 API reference <envoy_v3_api_msg_config.accesslog.v3.AccessLog>`

.. _config_access_log_format:

Format Rules
------------

Access log formats contain command operators that extract the relevant data and insert it.
They support two formats: :ref:`"format strings" <config_access_log_format_strings>` and
:ref:`"format dictionaries" <config_access_log_format_dictionaries>`. In both cases, the command operators
are used to extract the relevant data, which is then inserted into the specified log format.
Only one access log format may be specified at a time.

.. _config_access_log_format_strings:

Format Strings
--------------

Format strings are plain strings, specified using the ``format`` key. They may contain
either command operators or other characters interpreted as a plain string.
The access log formatter does not make any assumptions about a new line separator, so one
has to specified as part of the format string.
See the :ref:`default format <config_access_log_default_format>` for an example.

.. _config_access_log_default_format:

Default Format String
---------------------

If custom format string is not specified, Envoy uses the following default format:

.. code-block:: none

  [%START_TIME%] "%REQ(:METHOD)% %REQ(X-ENVOY-ORIGINAL-PATH?:PATH)% %PROTOCOL%"
  %RESPONSE_CODE% %RESPONSE_FLAGS% %BYTES_RECEIVED% %BYTES_SENT% %DURATION%
  %RESP(X-ENVOY-UPSTREAM-SERVICE-TIME)% "%REQ(X-FORWARDED-FOR)%" "%REQ(USER-AGENT)%"
  "%REQ(X-REQUEST-ID)%" "%REQ(:AUTHORITY)%" "%UPSTREAM_HOST%"\n

Example of the default Envoy access log format:

.. code-block:: none

  [2016-04-15T20:17:00.310Z] "POST /api/v1/locations HTTP/2" 204 - 154 0 226 100 "10.0.35.28"
  "nsq2http" "cc21d9b0-cf5c-432b-8c7e-98aeb7988cd2" "locations" "tcp://10.0.2.1:80"

.. _config_access_log_format_dictionaries:

Format Dictionaries
-------------------

Format dictionaries are dictionaries that specify a structured access log output format,
specified using the ``json_format`` or ``typed_json_format`` keys. This allows logs to be output in
a structured format such as JSON. Similar to format strings, command operators are evaluated and
their values inserted into the format dictionary to construct the log output.

For example, with the following format provided in the configuration as ``json_format``:

.. code-block:: json

  {
    "config": {
      "json_format": {
          "protocol": "%PROTOCOL%",
          "duration": "%DURATION%",
          "my_custom_header": "%REQ(MY_CUSTOM_HEADER)%"
      }
    }
  }

The following JSON object would be written to the log file:

.. code-block:: json

  {"protocol": "HTTP/1.1", "duration": "123", "my_custom_header": "value_of_MY_CUSTOM_HEADER"}

This allows you to specify a custom key for each command operator.

The ``typed_json_format`` differs from ``json_format`` in that values are rendered as JSON numbers,
booleans, and nested objects or lists where applicable. In the example, the request duration
would be rendered as the number ``123``.

Format dictionaries have the following restrictions:

* The dictionary must map strings to strings (specifically, strings to command operators). Nesting
  is supported.
* When using the ``typed_json_format`` command operators will only produce typed output if the
  command operator is the only string that appears in the dictionary value. For example,
  ``"%DURATION%"`` will log a numeric duration value, but ``"%DURATION%.0"`` will log a string
  value.

.. note::

  When using the ``typed_json_format``, integer values that exceed :math:`2^{53}` will be
  represented with reduced precision as they must be converted to floating point numbers.

.. _config_access_log_command_operators:

Command Operators
-----------------

Command operators are used to extract values that will be inserted into the access logs.
The same operators are used by different types of access logs (such as HTTP and TCP). Some
fields may have slightly different meanings, depending on what type of log it is. Differences
are noted.

Note that if a value is not set/empty, the logs will contain a ``-`` character or, for JSON logs,
the string ``"-"``. For typed JSON logs unset values are represented as ``null`` values and empty
strings are rendered as ``""``. :ref:`omit_empty_values
<envoy_v3_api_field_config.core.v3.SubstitutionFormatString.omit_empty_values>` option could be used
to omit empty values entirely.

Unless otherwise noted, command operators produce string outputs for typed JSON logs.

The following command operators are supported:

.. _config_access_log_format_start_time:

%START_TIME%
  HTTP/THRIFT
    Request start time including milliseconds.

  TCP
    Downstream connection start time including milliseconds.

  UDP
    UDP proxy session start time including milliseconds.

  START_TIME can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  In addition to that, START_TIME also accepts following specifiers:

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

  Examples of formatting START_TIME is as follows:

  .. code-block:: none

    %START_TIME(%Y/%m/%dT%H:%M:%S%z %s)%

    # To include millisecond fraction of the second (.000 ... .999). E.g. 1527590590.528.
    %START_TIME(%s.%3f)%

    %START_TIME(%s.%6f)%

    %START_TIME(%s.%9f)%

  In typed JSON logs, START_TIME is always rendered as a string.

%REQUEST_HEADERS_BYTES%
  HTTP
    Uncompressed bytes of request headers.

  TCP/UDP
    Not implemented (0).

%BYTES_RECEIVED%
  HTTP/THRIFT
    Body bytes received.

  TCP
    Downstream bytes received on connection.

  UDP
    Not implemented (0).

  Renders a numeric value in typed JSON logs.

%PROTOCOL%
  HTTP
    Protocol. Currently either *HTTP/1.1* *HTTP/2* or *HTTP/3*.

  TCP/UDP
    Not implemented ("-").

  In typed JSON logs, PROTOCOL will render the string ``"-"`` if the protocol is not
  available (e.g. in TCP logs).

%UPSTREAM_PROTOCOL%
  HTTP
    Upstream protocol. Currently either *HTTP/1.1* *HTTP/2* or *HTTP/3*.

  TCP/UDP
    Not implemented ("-").

  In typed JSON logs, UPSTREAM_PROTOCOL will render the string ``"-"`` if the protocol is not
  available (e.g. in TCP logs).

%RESPONSE_CODE%
  HTTP
    HTTP response code. Note that a response code of '0' means that the server never sent the
    beginning of a response. This generally means that the (downstream) client disconnected.

    Note that in the case of 100-continue responses, only the response code of the final headers
    will be logged. If a 100-continue is followed by a 200, the logged response will be 200.
    If a 100-continue results in a disconnect, the 100 will be logged.

  TCP/UDP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

.. _config_access_log_format_response_code_details:

%RESPONSE_CODE_DETAILS%
  HTTP
    HTTP response code details provides additional information about the response code, such as
    who set it (the upstream or envoy) and why.

  TCP/UDP
    Not implemented ("-")

.. _config_access_log_format_connection_termination_details:

%CONNECTION_TERMINATION_DETAILS%
  HTTP and TCP
    Connection termination details may provide additional information about why the connection was
    terminated by Envoy for L4 reasons.

%RESPONSE_HEADERS_BYTES%
  HTTP
    Uncompressed bytes of response headers.

  TCP/UDP
    Not implemented (0).

%RESPONSE_TRAILERS_BYTES%
  HTTP
    Uncompressed bytes of response trailers.

  TCP/UDP
    Not implemented (0).

%BYTES_SENT%
  HTTP/THRIFT
    Body bytes sent. For WebSocket connection it will also include response header bytes.

  TCP
    Downstream bytes sent on connection.

  UDP
    Not implemented (0).

%UPSTREAM_REQUEST_ATTEMPT_COUNT%
  HTTP
    Number of times the request is attempted upstream. Note that an attempt count of '0' means that
    the request was never attempted upstream.

  TCP
    Number of times the connection request is attempted upstream. Note that an attempt count of '0'
    means that the connection request was never attempted upstream.

  UDP
    Not implemented (0).

  Renders a numeric value in typed JSON logs.

%UPSTREAM_WIRE_BYTES_SENT%
  HTTP
    Total number of bytes sent to the upstream by the http stream.

  TCP
    Total number of bytes sent to the upstream by the tcp proxy.

  UDP
    Not implemented (0).

%UPSTREAM_WIRE_BYTES_RECEIVED%
  HTTP
    Total number of bytes received from the upstream by the http stream.

  TCP
    Total number of bytes received from the upstream by the tcp proxy.

  UDP
    Not implemented (0).

%UPSTREAM_HEADER_BYTES_SENT%
  HTTP
    Number of header bytes sent to the upstream by the http stream.

  TCP/UDP
    Not implemented (0).

%UPSTREAM_HEADER_BYTES_RECEIVED%
  HTTP
    Number of header bytes received from the upstream by the http stream.

  TCP/UDP
    Not implemented (0).

%DOWNSTREAM_WIRE_BYTES_SENT%
  HTTP
    Total number of bytes sent to the downstream by the http stream.

  TCP
    Total number of bytes sent to the downstream by the tcp proxy.

  UDP
    Not implemented (0).

%DOWNSTREAM_WIRE_BYTES_RECEIVED%
  HTTP
    Total number of bytes received from the downstream by the http stream. Envoy over counts sizes of received HTTP/1.1 pipelined requests by adding up bytes of requests in the pipeline to the one currently being processed.

  TCP
    Total number of bytes received from the downstream by the tcp proxy.

  UDP
    Not implemented (0).

%DOWNSTREAM_HEADER_BYTES_SENT%
  HTTP
    Number of header bytes sent to the downstream by the http stream.

  TCP/UDP
    Not implemented (0).

%DOWNSTREAM_HEADER_BYTES_RECEIVED%
  HTTP
    Number of header bytes received from the downstream by the http stream.

  TCP/UDP
    Not implemented (0).

  Renders a numeric value in typed JSON logs.

%DURATION%
  HTTP/THRIFT
    Total duration in milliseconds of the request from the start time to the last byte out.

  TCP
    Total duration in milliseconds of the downstream connection.

  UDP
    Not implemented (0).

  Renders a numeric value in typed JSON logs.

%REQUEST_DURATION%
  HTTP
    Total duration in milliseconds of the request from the start time to the last byte of
    the request received from the downstream.

  TCP/UDP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

%REQUEST_TX_DURATION%
  HTTP
    Total duration in milliseconds of the request from the start time to the last byte sent upstream.

  TCP/UDP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

%RESPONSE_DURATION%
  HTTP
    Total duration in milliseconds of the request from the start time to the first byte read from the
    upstream host.

  TCP/UDP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

%RESPONSE_TX_DURATION%
  HTTP
    Total duration in milliseconds of the request from the first byte read from the upstream host to the last
    byte sent downstream.

  TCP/UDP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

%DOWNSTREAM_HANDSHAKE_DURATION%
  HTTP
    Not implemented ("-").

  TCP
    Total duration in milliseconds from the start of the connection to the TLS handshake being completed.

  UDP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

.. _config_access_log_format_response_flags:

%RESPONSE_FLAGS%
  Additional details about the response or connection, if any. For TCP connections, the response codes mentioned in
  the descriptions do not apply. Possible values are:

  HTTP and TCP
    * **UH**: No healthy upstream hosts in upstream cluster in addition to 503 response code.
    * **UF**: Upstream connection failure in addition to 503 response code.
    * **UO**: Upstream overflow (:ref:`circuit breaking <arch_overview_circuit_break>`) in addition to 503 response code.
    * **NR**: No :ref:`route configured <arch_overview_http_routing>` for a given request in addition to 404 response code, or no matching filter chain for a downstream connection.
    * **URX**: The request was rejected because the :ref:`upstream retry limit (HTTP) <envoy_v3_api_field_config.route.v3.RetryPolicy.num_retries>`  or :ref:`maximum connect attempts (TCP) <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_connect_attempts>` was reached.
    * **NC**: Upstream cluster not found.
    * **DT**: When a request or connection exceeded :ref:`max_connection_duration <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.max_connection_duration>` or :ref:`max_downstream_connection_duration <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.max_downstream_connection_duration>`.
  HTTP only
    * **DC**: Downstream connection termination.
    * **LH**: Local service failed :ref:`health check request <arch_overview_health_checking>` in addition to 503 response code.
    * **UT**: Upstream request timeout in addition to 504 response code.
    * **LR**: Connection local reset in addition to 503 response code.
    * **UR**: Upstream remote reset in addition to 503 response code.
    * **UC**: Upstream connection termination in addition to 503 response code.
    * **DI**: The request processing was delayed for a period specified via :ref:`fault injection <config_http_filters_fault_injection>`.
    * **FI**: The request was aborted with a response code specified via :ref:`fault injection <config_http_filters_fault_injection>`.
    * **RL**: The request was ratelimited locally by the :ref:`HTTP rate limit filter <config_http_filters_rate_limit>` in addition to 429 response code.
    * **UAEX**: The request was denied by the external authorization service.
    * **RLSE**: The request was rejected because there was an error in rate limit service.
    * **IH**: The request was rejected because it set an invalid value for a
      :ref:`strictly-checked header <envoy_v3_api_field_extensions.filters.http.router.v3.Router.strict_check_headers>` in addition to 400 response code.
    * **SI**: Stream idle timeout in addition to 408 or 504 response code.
    * **DPE**: The downstream request had an HTTP protocol error.
    * **UPE**: The upstream response had an HTTP protocol error.
    * **UMSDR**: The upstream request reached max stream duration.
    * **OM**: Overload Manager terminated the request.
    * **DF**: The request was terminated due to DNS resolution failure.

  UDP
    Not implemented ("-").

%ROUTE_NAME%
  HTTP/TCP
    Name of the route.

  UDP
    Not implemented ("-").

%VIRTUAL_CLUSTER_NAME%
  HTTP*/gRPC
    Name of the matched Virtual Cluster (if any).

  TCP/UDP
    Not implemented ("-")

%UPSTREAM_HOST%
  Upstream host URL (e.g., tcp://ip:port for TCP connections).

%UPSTREAM_CLUSTER%
  Upstream cluster to which the upstream host belongs to. :ref:`alt_stat_name
  <envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` will be used if provided.

%UPSTREAM_LOCAL_ADDRESS%
  Local address of the upstream connection. If the address is an IP address it includes both
  address and port.

%UPSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%
  Local address of the upstream connection, without any port component.
  IP addresses are the only address type with a port component.

%UPSTREAM_LOCAL_PORT%
  Local port of the upstream connection.
  IP addresses are the only address type with a port component.

%UPSTREAM_REMOTE_ADDRESS%
  Remote address of the upstream connection. If the address is an IP address it includes both
  address and port.

%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%
  Remote address of the upstream connection, without any port component.
  IP addresses are the only address type with a port component.

%UPSTREAM_REMOTE_PORT%
  Remote port of the upstream connection.
  IP addresses are the only address type with a port component.

.. _config_access_log_format_upstream_transport_failure_reason:

%UPSTREAM_TRANSPORT_FAILURE_REASON%
  HTTP
    If upstream connection failed due to transport socket (e.g. TLS handshake), provides the failure
    reason from the transport socket. The format of this field depends on the configured upstream
    transport socket. Common TLS failures are in :ref:`TLS trouble shooting <arch_overview_ssl_trouble_shooting>`.

  TCP/UDP
    Not implemented ("-")

%DOWNSTREAM_REMOTE_ADDRESS%
  Remote address of the downstream connection. If the address is an IP address it includes both
  address and port.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%
  Remote address of the downstream connection, without any port component.
  IP addresses are the only address type with a port component.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_REMOTE_PORT%
  Remote port of the downstream connection.
  IP addresses are the only address type with a port component.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_DIRECT_REMOTE_ADDRESS%
  Direct remote address of the downstream connection. If the address is an IP address it includes both
  address and port.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%
  Direct remote address of the downstream connection, without any port component.
  IP addresses are the only address type with a port component.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_DIRECT_REMOTE_PORT%
  Direct remote port of the downstream connection.
  IP addresses are the only address type with a port component.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_LOCAL_ADDRESS%
  Local address of the downstream connection. If the address is an IP address it includes both
  address and port.

  If the original connection was redirected by iptables REDIRECT, this represents
  the original destination address restored by the
  :ref:`Original Destination Filter <config_listener_filters_original_dst>` using SO_ORIGINAL_DST socket option.
  If the original connection was redirected by iptables TPROXY, and the listener's transparent
  option was set to true, this represents the original destination address and port.

%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%
  Local address of the downstream connection, without any port component.
  IP addresses are the only address type with a port component.

%DOWNSTREAM_LOCAL_PORT%
  Local port of the downstream connection.
  IP addresses are the only address type with a port component.

.. _config_access_log_format_connection_id:

%CONNECTION_ID%
  An identifier for the downstream connection. It can be used to
  cross-reference TCP access logs across multiple log sinks, or to
  cross-reference timer-based reports for the same connection. The identifier
  is unique with high likelihood within an execution, but can duplicate across
  multiple instances or between restarts.

.. _config_access_log_format_stream_id:

%STREAM_ID%
  An identifier for the stream (HTTP request, long-live HTTP2 stream, TCP connection, etc.). It can be used to
  cross-reference TCP access logs across multiple log sinks, or to cross-reference timer-based reports for the same connection.
  Different with %CONNECTION_ID%, the identifier should be unique across multiple instances or between restarts.
  And it's value should be same with %REQ(X-REQUEST-ID)% for HTTP request.
  This should be used to replace %CONNECTION_ID% and %REQ(X-REQUEST-ID)% in most cases.

%GRPC_STATUS(X)%
  `gRPC status code <https://github.com/googleapis/googleapis/blob/master/google/rpc/code.proto>`_ formatted according to the optional parameter ``X``, which can be ``CAMEL_STRING``, ``SNAKE_STRING`` and ``NUMBER``.
  For example, if the grpc status is ``INVALID_ARGUMENT`` (represented by number 3), the formatter will return ``InvalidArgument`` for ``CAMEL_STRING``, ``INVALID_ARGUMENT`` for ``SNAKE_STRING`` and ``3`` for ``NUMBER``.
  If ``X`` isn't provided, ``CAMEL_STRING`` will be used.

%GRPC_STATUS_NUMBER%
  gRPC status code.

.. _config_access_log_format_req:

%REQ(X?Y):Z%
  HTTP
    An HTTP request header where X is the main HTTP header, Y is the alternative one, and Z is an
    optional parameter denoting string truncation up to Z characters long. The value is taken from
    the HTTP request header named X first and if it's not set, then request header Y is used. If
    none of the headers are present '-' symbol will be in the log.

  TCP/UDP
    Not implemented ("-").

%RESP(X?Y):Z%
  HTTP
    Same as **%REQ(X?Y):Z%** but taken from HTTP response headers.

  TCP/UDP
    Not implemented ("-").

%TRAILER(X?Y):Z%
  HTTP
    Same as **%REQ(X?Y):Z%** but taken from HTTP response trailers.

  TCP/UDP
    Not implemented ("-").

.. _config_access_log_format_dynamic_metadata:

%DYNAMIC_METADATA(NAMESPACE:KEY*):Z%
  HTTP
    :ref:`Dynamic Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where NAMESPACE is the filter namespace used when setting the metadata, KEY is an optional
    lookup key in the namespace with the option of specifying nested keys separated by ':',
    and Z is an optional parameter denoting string truncation up to Z characters long. Dynamic Metadata
    can be set by filters using the :repo:`StreamInfo <envoy/stream_info/stream_info.h>` API:
    *setDynamicMetadata*. The data will be logged as a JSON string. For example, for the following dynamic metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * %DYNAMIC_METADATA(com.test.my_filter)% will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * %DYNAMIC_METADATA(com.test.my_filter:test_key)% will log: ``foo``
    * %DYNAMIC_METADATA(com.test.my_filter:test_object)% will log: ``{"inner_key": "bar"}``
    * %DYNAMIC_METADATA(com.test.my_filter:test_object:inner_key)% will log: ``bar``
    * %DYNAMIC_METADATA(com.unknown_filter)% will log: ``-``
    * %DYNAMIC_METADATA(com.test.my_filter:unknown_key)% will log: ``-``
    * %DYNAMIC_METADATA(com.test.my_filter):25% will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  TCP
    Not implemented ("-").

  UDP
    For :ref:`UDP Proxy <config_udp_listener_filters_udp_proxy>`,
    when NAMESPACE is set to "udp.proxy.session", optional KEYs are as follows:

    * ``cluster_name``: Name of the cluster.
    * ``bytes_sent``: Total number of downstream bytes sent to the upstream in the session.
    * ``bytes_received``: Total number of downstream bytes received from the upstream in the session.
    * ``errors_sent``: Number of errors that have occurred when sending datagrams to the upstream in the session.
    * ``datagrams_sent``: Number of datagrams sent to the upstream successfully in the session.
    * ``datagrams_received``: Number of datagrams received from the upstream successfully in the session.

    Recommended session access log format for UDP proxy:

    .. code-block:: none

      [%START_TIME%] %DYNAMIC_METADATA(udp.proxy.session:cluster_name)%
      %DYNAMIC_METADATA(udp.proxy.session:bytes_sent)%
      %DYNAMIC_METADATA(udp.proxy.session:bytes_received)%
      %DYNAMIC_METADATA(udp.proxy.session:errors_sent)%
      %DYNAMIC_METADATA(udp.proxy.session:datagrams_sent)%
      %DYNAMIC_METADATA(udp.proxy.session:datagrams_received)%\n

    when NAMESPACE is set to "udp.proxy.proxy", optional KEYs are as follows:

    * ``bytes_sent``: Total number of downstream bytes sent to the upstream in UDP proxy.
    * ``bytes_received``: Total number of downstream bytes received from the upstream in UDP proxy.
    * ``errors_sent``: Number of errors that have occurred when sending datagrams to the upstream in UDP proxy.
    * ``errors_received``: Number of errors that have occurred when receiving datagrams from the upstream in UDP proxy.
    * ``datagrams_sent``: Number of datagrams sent to the upstream successfully in UDP proxy.
    * ``datagrams_received``: Number of datagrams received from the upstream successfully in UDP proxy.
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
      %DYNAMIC_METADATA(udp.proxy.proxy:session_total)%\n

  THRIFT
    For :ref:`Thrift Proxy <config_network_filters_thrift_proxy>`,
    NAMESPACE should be always set to "thrift.proxy", optional KEYs are as follows:

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
      %UPSTREAM_HOST%\n

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored.

  .. note::

   DYNAMIC_METADATA command operator will be deprecated in the future in favor of :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` operator.

.. _config_access_log_format_cluster_metadata:

%CLUSTER_METADATA(NAMESPACE:KEY*):Z%
  HTTP
    :ref:`Upstream cluster Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where NAMESPACE is the filter namespace used when setting the metadata, KEY is an optional
    lookup key in the namespace with the option of specifying nested keys separated by ':',
    and Z is an optional parameter denoting string truncation up to Z characters long. The data
    will be logged as a JSON string. For example, for the following dynamic metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * %CLUSTER_METADATA(com.test.my_filter)% will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * %CLUSTER_METADATA(com.test.my_filter:test_key)% will log: ``foo``
    * %CLUSTER_METADATA(com.test.my_filter:test_object)% will log: ``{"inner_key": "bar"}``
    * %CLUSTER_METADATA(com.test.my_filter:test_object:inner_key)% will log: ``bar``
    * %CLUSTER_METADATA(com.unknown_filter)% will log: ``-``
    * %CLUSTER_METADATA(com.test.my_filter:unknown_key)% will log: ``-``
    * %CLUSTER_METADATA(com.test.my_filter):25% will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  TCP/UDP/THRIFT
    Not implemented ("-").

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored.

  .. note::

   CLUSTER_METADATA command operator will be deprecated in the future in favor of :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` operator.

.. _config_access_log_format_upstream_host_metadata:

%UPSTREAM_METADATA(NAMESPACE:KEY*):Z%
  HTTP/TCP
    :ref:`Upstream host Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where NAMESPACE is the filter namespace used when setting the metadata, KEY is an optional
    lookup key in the namespace with the option of specifying nested keys separated by ':',
    and Z is an optional parameter denoting string truncation up to Z characters long. The data
    will be logged as a JSON string. For example, for the following upstream host metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * %UPSTREAM_METADATA(com.test.my_filter)% will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * %UPSTREAM_METADATA(com.test.my_filter:test_key)% will log: ``foo``
    * %UPSTREAM_METADATA(com.test.my_filter:test_object)% will log: ``{"inner_key": "bar"}``
    * %UPSTREAM_METADATA(com.test.my_filter:test_object:inner_key)% will log: ``bar``
    * %UPSTREAM_METADATA(com.unknown_filter)% will log: ``-``
    * %UPSTREAM_METADATA(com.test.my_filter:unknown_key)% will log: ``-``
    * %UPSTREAM_METADATA(com.test.my_filter):25% will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  UDP/THRIFT
    Not implemented ("-").

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored.

  .. note::

   UPSTREAM_METADATA command operator will be deprecated in the future in favor of :ref:`METADATA<envoy_v3_api_msg_extensions.formatter.metadata.v3.Metadata>` operator.

.. _config_access_log_format_filter_state:

%FILTER_STATE(KEY:F):Z%
  HTTP
    :ref:`Filter State <arch_overview_data_sharing_between_filters>` info, where the KEY is required to
    look up the filter state object. The serialized proto will be logged as JSON string if possible.
    If the serialized proto is unknown to Envoy it will be logged as protobuf debug string.
    Z is an optional parameter denoting string truncation up to Z characters long.
    F is an optional parameter used to indicate which method FilterState uses for serialization.
    If 'PLAIN' is set, the filter state object will be serialized as an unstructured string.
    If 'TYPED' is set or no F provided, the filter state object will be serialized as an JSON string.

  TCP/UDP
    Same as HTTP, the filter state is from connection instead of a L7 request.

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored

%UPSTREAM_FILTER_STATE(KEY:F):Z%
  HTTP
    Extracts filter state from upstream components like cluster or transport socket extensions.

    :ref:`Filter State <arch_overview_data_sharing_between_filters>` info, where the KEY is required to
    look up the filter state object. The serialized proto will be logged as JSON string if possible.
    If the serialized proto is unknown to Envoy it will be logged as protobuf debug string.
    Z is an optional parameter denoting string truncation up to Z characters long.
    F is an optional parameter used to indicate which method FilterState uses for serialization.
    If 'PLAIN' is set, the filter state object will be serialized as an unstructured string.
    If 'TYPED' is set or no F provided, the filter state object will be serialized as an JSON string.

  TCP/UDP
    Not implemented.

  .. note::

    This command operator is only available for :ref:`upstream_log <envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_log>`

%REQUESTED_SERVER_NAME%
  HTTP/TCP/THRIFT
    String value set on ssl connection socket for Server Name Indication (SNI)
  UDP
    Not implemented ("-").

%DOWNSTREAM_LOCAL_URI_SAN%
  HTTP/TCP/THRIFT
    The URIs present in the SAN of the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_URI_SAN%
  HTTP/TCP/THRIFT
    The URIs present in the SAN of the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_LOCAL_SUBJECT%
  HTTP/TCP/THRIFT
    The subject present in the local certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_SUBJECT%
  HTTP/TCP/THRIFT
    The subject present in the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_ISSUER%
  HTTP/TCP/THRIFT
    The issuer present in the peer certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_TLS_SESSION_ID%
  HTTP/TCP/THRIFT
    The session ID for the established downstream TLS connection.
  UDP
    Not implemented (0).

%DOWNSTREAM_TLS_CIPHER%
  HTTP/TCP/THRIFT
    The OpenSSL name for the set of ciphers used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_TLS_VERSION%
  HTTP/TCP/THRIFT
    The TLS version (e.g., ``TLSv1.2``, ``TLSv1.3``) used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_FINGERPRINT_256%
  HTTP/TCP/THRIFT
    The hex-encoded SHA256 fingerprint of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_FINGERPRINT_1%
  HTTP/TCP/THRIFT
    The hex-encoded SHA1 fingerprint of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_SERIAL%
  HTTP/TCP/THRIFT
    The serial number of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

%DOWNSTREAM_PEER_CERT%
  HTTP/TCP/THRIFT
    The client certificate in the URL-encoded PEM format used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

.. _config_access_log_format_downstream_peer_cert_v_start:

%DOWNSTREAM_PEER_CERT_V_START%
  HTTP/TCP/THRIFT
    The validity start date of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

  DOWNSTREAM_PEER_CERT_V_START can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

.. _config_access_log_format_downstream_peer_cert_v_end:

%DOWNSTREAM_PEER_CERT_V_END%
  HTTP/TCP/THRIFT
    The validity end date of the client certificate used to establish the downstream TLS connection.
  UDP
    Not implemented ("-").

  DOWNSTREAM_PEER_CERT_V_END can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

%UPSTREAM_PEER_SUBJECT%
  HTTP/TCP/THRIFT
    The subject present in the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

%UPSTREAM_PEER_ISSUER%
  HTTP/TCP/THRIFT
    The issuer present in the peer certificate used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

%UPSTREAM_TLS_SESSION_ID%
  HTTP/TCP/THRIFT
    The session ID for the established upstream TLS connection.
  UDP
    Not implemented (0).

%UPSTREAM_TLS_CIPHER%
  HTTP/TCP/THRIFT
    The OpenSSL name for the set of ciphers used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

%UPSTREAM_TLS_VERSION%
  HTTP/TCP/THRIFT
    The TLS version (e.g., ``TLSv1.2``, ``TLSv1.3``) used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

%UPSTREAM_PEER_CERT%
  HTTP/TCP/THRIFT
    The server certificate in the URL-encoded PEM format used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

.. _config_access_log_format_upstream_peer_cert_v_start:

%UPSTREAM_PEER_CERT_V_START%
  HTTP/TCP/THRIFT
    The validity start date of the upstream server certificate used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

  UPSTREAM_PEER_CERT_V_START can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

.. _config_access_log_format_upstream_peer_cert_v_end:

%UPSTREAM_PEER_CERT_V_END%
  HTTP/TCP/THRIFT
    The validity end date of the upstream server certificate used to establish the upstream TLS connection.
  UDP
    Not implemented ("-").

  UPSTREAM_PEER_CERT_V_END can be customized using a `format string <https://en.cppreference.com/w/cpp/io/manip/put_time>`_.
  See :ref:`START_TIME <config_access_log_format_start_time>` for additional format specifiers and examples.

%HOSTNAME%
  The system hostname.

%LOCAL_REPLY_BODY%
  The body text for the requests rejected by the Envoy.

%FILTER_CHAIN_NAME%
  The :ref:`network filter chain name <envoy_v3_api_field_config.listener.v3.FilterChain.name>` of the downstream connection.

%ENVIRONMENT(X):Z%
  Environment value of environment variable X. If no valid environment variable X, '-' symbol will be used.
  Z is an optional parameter denoting string truncation up to Z characters long.
