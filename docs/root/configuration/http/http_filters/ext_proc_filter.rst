.. _config_http_filters_ext_proc:

External Processing
===================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExternalProcessor``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3.ExternalProcessor>`

The external processing filter connects an external service, called an "external processor,"
to the filter chain. The processing service itself implements a gRPC interface that allows
it to respond to events in the lifecycle of an HTTP request / response by examining
and modifying the headers, body, and trailers of each message, or by returning a brand-new response.

The protocol itself is based on a bidirectional gRPC stream. Envoy will send the
external processor
:ref:`ProcessingRequest <envoy_v3_api_msg_service.ext_proc.v3.ProcessingRequest>`
messages, and the processor must reply with
:ref:`ProcessingResponse <envoy_v3_api_msg_service.ext_proc.v3.ProcessingResponse>`
messages.

Configuration options are provided to control which events are sent to the processor.
This way, the processor may receive headers, body, and trailers for both
request and response in any combination. The processor may also change this configuration
on a message-by-message basis. This allows for the construction of sophisticated processors
that decide how to respond to each message individually to eliminate unnecessary
stream requests from the proxy.

The updated list of supported features can be found on the
:ref:`reference page <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3.ExternalProcessor>`.

Statistics
----------
This filter outputs statistics in the
``http.<stat_prefix>.ext_proc.`` namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

The following statistics are supported:

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  streams_started, Counter, The number of gRPC streams that have been started to send to the external processing service
  stream_msgs_sent, Counter, The number of messages sent on those streams
  stream_msgs_received, Counter, The number of messages received on those streams
  spurious_msgs_received, Counter, The number of unexpected messages received that violated the protocol
  streams_closed, Counter, The number of streams successfully closed on either end
  streams_failed, Counter, The number of times a stream produced a gRPC error
  failure_mode_allowed, Counter, The number of times an error was ignored due to configuration
  message_timeouts, Counter, The number of times a message failed to receive a response within the configured timeout
  rejected_header_mutations, Counter, The number of rejected header mutations
  override_message_timeout_received, Counter, The number of override_message_timeout messages received
  override_message_timeout_ignored, Counter, The number of override_message_timeout messages ignored
  clear_route_cache_ignored, Counter, The number of clear cache request that were ignored
  clear_route_cache_disabled, Counter, The number of clear cache requests that were rejected from being disabled
  clear_route_cache_upstream_ignored, Counter, The number of clear cache request that were ignored if the filter is in upstream
  send_immediate_resp_upstream_ignored, Counter, The number of send immediate response messages that were ignored if the filter is in upstream

Access Log Fields
------------------

The external processing filter exposes processing statistics and metadata for use in access logs
through the filter state object named ``envoy.filters.http.ext_proc``. This information includes
gRPC call latencies, status codes, and byte counts that can be used for monitoring and debugging
external processor performance.

The filter state supports three serialization modes:

* **PLAIN**: Comma-separated ``key:value`` pairs in abbreviated format.
* **TYPED**: JSON object with descriptive field names.
* **FIELD**: Individual field access by name.

Available field names:

.. csv-table::
  :header: Field Name, Type, Description
  :widths: 2, 1, 3

  request_header_latency_us, Integer, Latency in microseconds for request header processing
  request_header_call_status, Integer, gRPC status code for request header call
  request_body_call_count, Integer, Number of request body chunks processed
  request_body_total_latency_us, Integer, Total latency for all request body calls in microseconds
  request_body_max_latency_us, Integer, Maximum latency among request body calls in microseconds
  request_body_last_call_status, Integer, gRPC status of the last request body call
  request_trailer_latency_us, Integer, Latency for request trailer processing in microseconds
  request_trailer_call_status, Integer, gRPC status code for request trailer call
  response_header_latency_us, Integer, Latency for response header processing in microseconds
  response_header_call_status, Integer, gRPC status code for response header call
  response_body_call_count, Integer, Number of response body chunks processed
  response_body_total_latency_us, Integer, Total latency for all response body calls in microseconds
  response_body_max_latency_us, Integer, Maximum latency among response body calls in microseconds
  response_body_last_call_status, Integer, gRPC status of the last response body call
  response_trailer_latency_us, Integer, Latency for response trailer processing in microseconds
  response_trailer_call_status, Integer, gRPC status code for response trailer call
  bytes_sent, Integer, Total bytes sent to external processor (Envoy gRPC only)
  bytes_received, Integer, Total bytes received from external processor (Envoy gRPC only)

Example usage in access log configuration:

.. code-block:: yaml

  access_log:
    - name: envoy.access_loggers.stdout
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.stream.v3.StdoutAccessLog
        log_format:
          json_format:
            # Individual field access
            ext_proc_header_latency: "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_header_latency_us)%"
            ext_proc_body_calls: "%FILTER_STATE(envoy.filters.http.ext_proc:FIELD:request_body_call_count)%"
            # Full structured data
            ext_proc_all_stats: "%FILTER_STATE(envoy.filters.http.ext_proc:TYPED)%"
            # Compact format
            ext_proc_summary: "%FILTER_STATE(envoy.filters.http.ext_proc:PLAIN)%"

.. note::

  The ``bytes_sent`` and ``bytes_received`` fields are only populated when using Envoy gRPC client type.
  For Google gRPC client type, these fields will be 0.

.. note::

  gRPC status codes follow the standard `gRPC status codes <https://grpc.github.io/grpc/core/md_doc_statuscodes.html>`_:
  0 = OK, 1 = CANCELLED, 2 = UNKNOWN, 3 = INVALID_ARGUMENT, 4 = DEADLINE_EXCEEDED, etc.
