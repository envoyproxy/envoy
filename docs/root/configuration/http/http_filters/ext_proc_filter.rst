.. _config_http_filters_ext_proc:

External Processing
===================
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ext_proc.v3.ExternalProcessor``.
* :ref:`Http filter v3 API reference <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3.ExternalProcessor>`

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

This filter is a work in progress. Most of the major bits of functionality
are complete. The updated list of supported features and implementation status may
be found on the :ref:`reference page <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3.ExternalProcessor>`.

Statistics
----------
This filter outputs statistics in the
*http.<stat_prefix>.ext_proc.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

The following statistics are supported:

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  streams_started, Counter, The number of gRPC streams that have been started to send to the external processing service
  streams_msgs_sent, Counter, The number of messages sent on those streams
  streams_msgs_received, Counter, The number of messages received on those streams
  spurious_msgs_received, Counter, The number of unexpected messages received that violated the protocol
  streams_closed, Counter, The number of streams successfully closed on either end
  streams_failed, Counter, The number of times a stream produced a gRPC error
  failure_mode_allowed, Counter, The number of times an error was ignored due to configuration
  message_timeouts, Counter, The number of times a message failed to receive a response within the configured timeout
  rejected_header_mutations, Counter, The number of rejected header mutations
