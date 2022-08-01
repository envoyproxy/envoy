.. _config_http_filters_tap:

Tap
===

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.tap.v3.Tap>`

.. attention::

  The tap filter is experimental and is currently under active development. There is currently a
  very limited set of match conditions, output configuration, output sinks, etc. Capabilities will
  be expanded over time and the configuration structures are likely to change.

The HTTP tap filter is used to interpose on and record HTTP traffic. At a high level, the
configuration is composed of two pieces:

1. :ref:`Match configuration <envoy_v3_api_msg_config.tap.v3.MatchPredicate>`: a list of
   conditions under which the filter will match an HTTP request and begin a tap session.
2. :ref:`Output configuration <envoy_v3_api_msg_config.tap.v3.OutputConfig>`: a list of output
   sinks that the filter will write the matched and tapped data to.

Each of these concepts will be covered incrementally over the course of several example
configurations in the following section.

Example configuration
---------------------

Example filter configuration:

.. code-block:: yaml

  name: envoy.filters.http.tap
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
    common_config:
      admin_config:
        config_id: test_config_id

The previous snippet configures the filter for control via the :http:post:`/tap` admin handler.
See the following section for more details.

.. _config_http_filters_tap_admin_handler:

Admin handler
-------------

When the HTTP filter specifies an :ref:`admin_config
<envoy_v3_api_msg_extensions.common.tap.v3.AdminConfig>`, it is configured for admin control and
the :http:post:`/tap` admin handler will be installed. The admin handler can be used for live
tapping and debugging of HTTP traffic. It works as follows:

1. A POST request is used to provide a valid tap configuration. The POST request body can be either
   the JSON or YAML representation of the :ref:`TapConfig
   <envoy_v3_api_msg_config.tap.v3.TapConfig>` message.
2. If the POST request is accepted, Envoy will stream :ref:`HttpBufferedTrace
   <envoy_v3_api_msg_data.tap.v3.HttpBufferedTrace>` messages (serialized to JSON) until the admin
   request is terminated.

.. attention::

  If using HTTP/1.1 to communicate with the admin endpoint, it is important to not use Unix Domain
  Sockets (UDS) as the underlying transport. This is because UDS do not support "early close
  detection" which means that when the client is disconnected it may take a substantial amount of
  time for Envoy to realize the connection has been terminated (typically when the next streamed
  message is written). During this time period it is currently impossible to connect a new tap. To
  work around this either use HTTP/2 or use TCP.

An example POST body:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_config:
      and_match:
        rules:
          - http_request_headers_match:
              headers:
                - name: foo
                  string_match:
                    exact: bar
          - http_response_headers_match:
              headers:
                - name: bar
                  string_match:
                    exact: baz
    output_config:
      sinks:
        - streaming_admin: {}

The preceding configuration instructs the tap filter to match any HTTP requests in which a request
header ``foo: bar`` is present AND a response header ``bar: baz`` is present. If both of these
conditions are met, the request will be tapped and streamed out the admin endpoint.

Another example POST body:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_config:
      or_match:
        rules:
          - http_request_headers_match:
              headers:
                - name: foo
                  string_match:
                    exact: bar
          - http_response_headers_match:
              headers:
                - name: bar
                  string_match:
                    exact: baz
    output_config:
      sinks:
        - streaming_admin: {}

The preceding configuration instructs the tap filter to match any HTTP requests in which a request
header ``foo: bar`` is present OR a response header ``bar: baz`` is present. If either of these
conditions are met, the request will be tapped and streamed out the admin endpoint.

Another example POST body:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_config:
      any_match: true
    output_config:
      sinks:
        - streaming_admin: {}

The preceding configuration instructs the tap filter to match any HTTP requests. All requests will
be tapped and streamed out the admin endpoint.

Another example POST body:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_config:
      and_match:
        rules:
          - http_request_headers_match:
              headers:
                - name: foo
                  string_match:
                    exact: bar
          - http_request_generic_body_match:
              patterns:
                - string_match: test
                - binary_match: 3q2+7w==
              bytes_limit: 128
          - http_response_generic_body_match:
              patterns:
                - binary_match: vu8=
              bytes_limit: 64
    output_config:
      sinks:
        - streaming_admin: {}

The preceding configuration instructs the tap filter to match any HTTP requests in which a request
header ``foo: bar`` is present AND request body contains string ``test`` and hex bytes ``deadbeef`` (``3q2+7w==`` in base64 format)
in the first 128 bytes AND response body contains hex bytes ``beef`` (``vu8=`` in base64 format) in the first 64 bytes. If all of these
conditions are met, the request will be tapped and streamed out to the admin endpoint.

.. attention::

  Searching for patterns in HTTP body is potentially cpu intensive. For each specified pattern, http body is scanned byte by byte to find a match.
  If multiple patterns are specified, the process is repeated for each pattern. If location of a pattern is known, ``bytes_limit`` should be specified
  to scan only part of the http body.

Output format
-------------

Each output sink has an associated :ref:`format
<envoy_v3_api_enum_config.tap.v3.OutputSink.Format>`. The default format is
:ref:`JSON_BODY_AS_BYTES
<envoy_v3_api_enum_value_config.tap.v3.OutputSink.Format.JSON_BODY_AS_BYTES>`. This format is
easy to read JSON, but has the downside that body data is base64 encoded. In the case that the tap
is known to be on human readable data, the :ref:`JSON_BODY_AS_STRING
<envoy_v3_api_enum_value_config.tap.v3.OutputSink.Format.JSON_BODY_AS_STRING>` format may be
more user friendly. See the reference documentation for more information on other available formats.

An example of a streaming admin tap configuration that uses the :ref:`JSON_BODY_AS_STRING
<envoy_v3_api_enum_value_config.tap.v3.OutputSink.Format.JSON_BODY_AS_STRING>` format:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_config:
      any_match: true
    output_config:
      sinks:
        - format: JSON_BODY_AS_STRING
          streaming_admin: {}

Buffering Data
--------------

Buffering data in tap requests can be done at two levels of granularity - buffering individual traces (downstream request & upstream response bodies) or buffering a set of traces.
Both levels of granularity have separate controls to limit the amount of data buffered.

When buffering individual traces, Envoy will limit the amount of body data that is tapped to avoid exhausting server memory.
The default limit is 1KiB for both received (request) and transmitted (response) data. This is
configurable via the :ref:`max_buffered_rx_bytes
<envoy_v3_api_field_config.tap.v3.OutputConfig.max_buffered_rx_bytes>` and
:ref:`max_buffered_tx_bytes
<envoy_v3_api_field_config.tap.v3.OutputConfig.max_buffered_tx_bytes>` settings.

.. _config_http_filters_tap_streaming:

Envoy also supports buffering multiple traces internally via the ``buffered_admin`` sink type.
This form of buffering is particularly useful for taps specifying a match configuration that is satisfied frequently.
The post body using a buffered admin sink should specify ``max_traces`` which is the number of traces to buffer,
and can optionally specify a ``timeout`` in seconds (Protobuf Duration), which is the maximum time the server
should wait to accumulate ``max_traces`` before flushing the traces buffered so far to the client. Each individual
buffered trace also adheres to the single trace buffer limits from above. This buffering behavior can also be implemented client side but
requires non-trivial code for interpreting trace streams as they are not delimited.
An example of a buffered admin tap configuration:

.. code-block:: yaml

  config_id: test_config_id
  tap_config:
    match_config:
      any_match: true
    output_config:
      sinks:
        - buffered_admin:
            max_traces: 3
            timeout: 0.2s

Streaming matching
------------------

The tap filter supports "streaming matching." This means that instead of waiting until the end of
the request/response sequence, the filter will match incrementally as the request proceeds. I.e.,
first the request headers will be matched, then the request body if present, then the request
trailers if present, then the response headers if present, etc.

The filter additionally supports optional streamed output which is governed by the :ref:`streaming
<envoy_v3_api_field_config.tap.v3.OutputConfig.streaming>` setting. If this setting is false
(the default), Envoy will emit :ref:`fully buffered traces
<envoy_v3_api_msg_data.tap.v3.HttpBufferedTrace>`. Users are likely to find this format easier
to interact with for simple cases.

In cases where fully buffered traces are not practical (e.g., very large request and responses,
long lived streaming APIs, etc.), the streaming setting can be set to true, and Envoy will emit
multiple :ref:`streamed trace segments <envoy_v3_api_msg_data.tap.v3.HttpStreamedTraceSegment>` for
each tap. In this case, it is required that post-processing is performed to stitch all of the trace
segments back together into a usable form. Also note that binary protobuf is not a self-delimiting
format. If binary protobuf output is desired, the :ref:`PROTO_BINARY_LENGTH_DELIMITED
<envoy_v3_api_enum_value_config.tap.v3.OutputSink.Format.PROTO_BINARY_LENGTH_DELIMITED>` output
format should be used.

An static filter configuration to enable streaming output looks like:

.. code-block:: yaml

  name: envoy.filters.http.tap
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.tap.v3.Tap
    common_config:
      static_config:
        match_config:
          http_response_headers_match:
            headers:
              - name: bar
                string_match:
                  exact: baz
        output_config:
          streaming: true
          sinks:
            - format: PROTO_BINARY_LENGTH_DELIMITED
              file_per_tap:
                path_prefix: /tmp/

The previous configuration will match response headers, and as such will buffer request headers,
body, and trailers until a match can be determined (buffered data limits still apply as described
in the previous section). If a match is determined, buffered data will be flushed in individual
trace segments and then the rest of the tap will be streamed as data arrives. The messages output
might look like this:

.. code-block:: yaml

  http_streamed_trace_segment:
    trace_id: 1
    request_headers:
      headers:
        - key: a
          value: b

.. code-block:: yaml

  http_streamed_trace_segment:
    trace_id: 1
    request_body_chunk:
      as_bytes: aGVsbG8=

Etc.

Statistics
----------

The tap filter outputs statistics in the *http.<stat_prefix>.tap.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_tapped, Counter, Total requests that matched and were tapped
