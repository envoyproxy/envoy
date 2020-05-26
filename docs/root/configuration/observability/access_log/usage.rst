.. _config_access_log:

Access logging
==============

Configuration
-------------------------

Access logs are configured as part of the :ref:`HTTP connection manager config
<config_http_conn_man>` or :ref:`TCP Proxy <config_network_filters_tcp_proxy>`.

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
  is not currently supported.
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
strings are rendered as ``""``.

Unless otherwise noted, command operators produce string outputs for typed JSON logs.

The following command operators are supported:

.. _config_access_log_format_start_time:

%START_TIME%
  HTTP
    Request start time including milliseconds.

  TCP
    Downstream connection start time including milliseconds.

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

%BYTES_RECEIVED%
  HTTP
    Body bytes received.

  TCP
    Downstream bytes received on connection.

  Renders a numeric value in typed JSON logs.

%PROTOCOL%
  HTTP
    Protocol. Currently either *HTTP/1.1* or *HTTP/2*.

  TCP
    Not implemented ("-").

  In typed JSON logs, PROTOCOL will render the string ``"-"`` if the protocol is not
  available (e.g. in TCP logs).

%RESPONSE_CODE%
  HTTP
    HTTP response code. Note that a response code of '0' means that the server never sent the
    beginning of a response. This generally means that the (downstream) client disconnected.

  TCP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

.. _config_access_log_format_response_code_details:

%RESPONSE_CODE_DETAILS%
  HTTP
    HTTP response code details provides additional information about the response code, such as
    who set it (the upstream or envoy) and why.

  TCP
    Not implemented ("-")

%BYTES_SENT%
  HTTP
    Body bytes sent. For WebSocket connection it will also include response header bytes.

  TCP
    Downstream bytes sent on connection.

  Renders a numeric value in typed JSON logs.

%DURATION%
  HTTP
    Total duration in milliseconds of the request from the start time to the last byte out.

  TCP
    Total duration in milliseconds of the downstream connection.

  Renders a numeric value in typed JSON logs.

%REQUEST_DURATION%
  HTTP
    Total duration in milliseconds of the request from the start time to the last byte of
    the request received from the downstream.

  TCP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

%RESPONSE_DURATION%
  HTTP
    Total duration in milliseconds of the request from the start time to the first byte read from the
    upstream host.

  TCP
    Not implemented ("-").

  Renders a numeric value in typed JSON logs.

%RESPONSE_TX_DURATION%
  HTTP
    Total duration in milliseconds of the request from the first byte read from the upstream host to the last
    byte sent downstream.

  TCP
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
    * **SI**: Stream idle timeout in addition to 408 response code.
    * **DPE**: The downstream request had an HTTP protocol error.
    * **UMSDR**: The upstream request reached to max stream duration.

%ROUTE_NAME%
  Name of the route.

%UPSTREAM_HOST%
  Upstream host URL (e.g., tcp://ip:port for TCP connections).

%UPSTREAM_CLUSTER%
  Upstream cluster to which the upstream host belongs to.

%UPSTREAM_LOCAL_ADDRESS%
  Local address of the upstream connection. If the address is an IP address it includes both
  address and port.

.. _config_access_log_format_upstream_transport_failure_reason:

%UPSTREAM_TRANSPORT_FAILURE_REASON%
  HTTP
    If upstream connection failed due to transport socket (e.g. TLS handshake), provides the failure
    reason from the transport socket. The format of this field depends on the configured upstream
    transport socket. Common TLS failures are in :ref:`TLS trouble shooting <arch_overview_ssl_trouble_shooting>`.

  TCP
    Not implemented ("-")

%DOWNSTREAM_REMOTE_ADDRESS%
  Remote address of the downstream connection. If the address is an IP address it includes both
  address and port.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%
  Remote address of the downstream connection. If the address is an IP address the output does
  *not* include port.

  .. note::

    This may not be the physical remote address of the peer if the address has been inferred from
    :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>` or :ref:`x-forwarded-for
    <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_DIRECT_REMOTE_ADDRESS%
  Direct remote address of the downstream connection. If the address is an IP address it includes both
  address and port.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>`
    or :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

%DOWNSTREAM_DIRECT_REMOTE_ADDRESS_WITHOUT_PORT%
  The direct remote address of the downstream connection. If the address is an IP address the output does
  *not* include port.

  .. note::

    This is always the physical remote address of the peer even if the downstream remote address has
    been inferred from :ref:`proxy proto <envoy_v3_api_field_config.listener.v3.FilterChain.use_proxy_proto>`
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
    Same as **%DOWNSTREAM_LOCAL_ADDRESS%** excluding port if the address is an IP address.

%GRPC_STATUS%
  gRPC status code which is easy to interpret with text message corresponding with number.

%DOWNSTREAM_LOCAL_PORT%
    Similar to **%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%**, but only extracts the port portion of the **%DOWNSTREAM_LOCAL_ADDRESS%**

%REQ(X?Y):Z%
  HTTP
    An HTTP request header where X is the main HTTP header, Y is the alternative one, and Z is an
    optional parameter denoting string truncation up to Z characters long. The value is taken from
    the HTTP request header named X first and if it's not set, then request header Y is used. If
    none of the headers are present '-' symbol will be in the log.

  TCP
    Not implemented ("-").

%RESP(X?Y):Z%
  HTTP
    Same as **%REQ(X?Y):Z%** but taken from HTTP response headers.

  TCP
    Not implemented ("-").

%TRAILER(X?Y):Z%
  HTTP
    Same as **%REQ(X?Y):Z%** but taken from HTTP response trailers.

  TCP
    Not implemented ("-").

%DYNAMIC_METADATA(NAMESPACE:KEY*):Z%
  HTTP
    :ref:`Dynamic Metadata <envoy_v3_api_msg_config.core.v3.Metadata>` info,
    where NAMESPACE is the filter namespace used when setting the metadata, KEY is an optional
    lookup up key in the namespace with the option of specifying nested keys separated by ':',
    and Z is an optional parameter denoting string truncation up to Z characters long. Dynamic Metadata
    can be set by filters using the :repo:`StreamInfo <include/envoy/stream_info/stream_info.h>` API:
    *setDynamicMetadata*. The data will be logged as a JSON string. For example, for the following dynamic metadata:

    ``com.test.my_filter: {"test_key": "foo", "test_object": {"inner_key": "bar"}}``

    * %DYNAMIC_METADATA(com.test.my_filter)% will log: ``{"test_key": "foo", "test_object": {"inner_key": "bar"}}``
    * %DYNAMIC_METADATA(com.test.my_filter:test_key)% will log: ``"foo"``
    * %DYNAMIC_METADATA(com.test.my_filter:test_object)% will log: ``{"inner_key": "bar"}``
    * %DYNAMIC_METADATA(com.test.my_filter:test_object:inner_key)% will log: ``"bar"``
    * %DYNAMIC_METADATA(com.unknown_filter)% will log: ``-``
    * %DYNAMIC_METADATA(com.test.my_filter:unknown_key)% will log: ``-``
    * %DYNAMIC_METADATA(com.test.my_filter):25% will log (truncation at 25 characters): ``{"test_key": "foo", "test``

  TCP
    Not implemented ("-").

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored

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

  TCP
    Same as HTTP, the filter state is from connection instead of a L7 request.

  .. note::

    For typed JSON logs, this operator renders a single value with string, numeric, or boolean type
    when the referenced key is a simple value. If the referenced key is a struct or list value, a
    JSON struct or list is rendered. Structs and lists may be nested. In any event, the maximum
    length is ignored

%REQUESTED_SERVER_NAME%
  HTTP
    String value set on ssl connection socket for Server Name Indication (SNI)
  TCP
    String value set on ssl connection socket for Server Name Indication (SNI)

%DOWNSTREAM_LOCAL_URI_SAN%
  HTTP
    The URIs present in the SAN of the local certificate used to establish the downstream TLS connection.
  TCP
    The URIs present in the SAN of the local certificate used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_URI_SAN%
  HTTP
    The URIs present in the SAN of the peer certificate used to establish the downstream TLS connection.
  TCP
    The URIs present in the SAN of the peer certificate used to establish the downstream TLS connection.

%DOWNSTREAM_LOCAL_SUBJECT%
  HTTP
    The subject present in the local certificate used to establish the downstream TLS connection.
  TCP
    The subject present in the local certificate used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_SUBJECT%
  HTTP
    The subject present in the peer certificate used to establish the downstream TLS connection.
  TCP
    The subject present in the peer certificate used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_ISSUER%
  HTTP
    The issuer present in the peer certificate used to establish the downstream TLS connection.
  TCP
    The issuer present in the peer certificate used to establish the downstream TLS connection.

%DOWNSTREAM_TLS_SESSION_ID%
  HTTP
    The session ID for the established downstream TLS connection.
  TCP
    The session ID for the established downstream TLS connection.

%DOWNSTREAM_TLS_CIPHER%
  HTTP
    The OpenSSL name for the set of ciphers used to establish the downstream TLS connection.
  TCP
    The OpenSSL name for the set of ciphers used to establish the downstream TLS connection.

%DOWNSTREAM_TLS_VERSION%
  HTTP
    The TLS version (e.g., ``TLSv1.2``, ``TLSv1.3``) used to establish the downstream TLS connection.
  TCP
    The TLS version (e.g., ``TLSv1.2``, ``TLSv1.3``) used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_FINGERPRINT_256%
  HTTP
    The hex-encoded SHA256 fingerprint of the client certificate used to establish the downstream TLS connection.
  TCP
    The hex-encoded SHA256 fingerprint of the client certificate used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_SERIAL%
  HTTP
    The serial number of the client certificate used to establish the downstream TLS connection.
  TCP
    The serial number of the client certificate used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_CERT%
  HTTP
    The client certificate in the URL-encoded PEM format used to establish the downstream TLS connection.
  TCP
    The client certificate in the URL-encoded PEM format used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_CERT_V_START%
  HTTP
    The validity start date of the client certificate used to establish the downstream TLS connection.
  TCP
    The validity start date of the client certificate used to establish the downstream TLS connection.

%DOWNSTREAM_PEER_CERT_V_END%
  HTTP
    The validity end date of the client certificate used to establish the downstream TLS connection.
  TCP
    The validity end date of the client certificate used to establish the downstream TLS connection.

%HOSTNAME%
  The system hostname.

%LOCAL_REPLY_BODY%
  The body text for the requests rejected by the Envoy.
