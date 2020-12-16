.. _config_http_filters_ext_proc:

External Processing
===================
* :ref:`Http filter v3 API reference <envoy_v3_api_msg_extensions.filters.http.ext_proc.v3alpha.ExternalProcessor>`
* This filter should be configured with the name *envoy.filters.http.ext_proc*

The external processing filter calls an external gRPC service to enable it to participate in
HTTP filter chain processing. The filter is called using a gRPC bidirectional stream, and allows
the filter to make decisions in real time about what parts of the HTTP request / response stream
are sent to the filter for processing.

The protocol itself is based on a bidirectional gRPC stream. Envoy will send the
server 
:ref:`ProcessingRequest <envoy_v3_api_msg_service.ext_proc.v3alpha.ProcessingRequest>`
messages, and the server must reply with 
:ref:`ProcessingResponse <envoy_v3_api_msg_service.ext_proc.v3alpha.ProcessingResponse>`.

This filter is a work in progress. In its current state, it actually does nothing.

Statistics
----------
This filter outputs statistics in the
*http.<stat_prefix>.ext_proc.* namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager. Statistics are specific to the concurrency
controllers.

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
