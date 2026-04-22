.. _config_network_filters_ext_proc:

External Processing
===================

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.ext_proc.v3.NetworkExternalProcessor``.
* :ref:`Network filter v3 API reference <envoy_v3_api_msg_extensions.filters.network.ext_proc.v3.NetworkExternalProcessor>`
* :ref:`Network external processing service API reference <envoy_v3_api_msg_service.network_ext_proc.v3.ProcessingRequest>`

The network external processing filter connects an external gRPC service to Envoy's L4 data path.
The service can inspect and modify raw network payloads flowing in either direction
(client->upstream and upstream->client), and can direct Envoy to continue, gracefully close,
or reset a connection.

Envoy and the external processor communicate over a bidirectional gRPC stream using
:ref:`ProcessingRequest <envoy_v3_api_msg_service.network_ext_proc.v3.ProcessingRequest>` and
:ref:`ProcessingResponse <envoy_v3_api_msg_service.network_ext_proc.v3.ProcessingResponse>`
messages.

Processing model
----------------

The filter can independently process read and write directions via
:ref:`processing_mode <envoy_v3_api_field_extensions.filters.network.ext_proc.v3.NetworkExternalProcessor.processing_mode>`:

* ``process_read: STREAMED`` sends downstream->upstream data to the processor.
* ``process_write: STREAMED`` sends upstream->downstream data to the processor.
* ``SKIP`` bypasses processing for that direction.

For each message:

* ``data_processing_status: UNMODIFIED`` keeps the original bytes.
* ``data_processing_status: MODIFIED`` applies the response payload bytes.
* ``connection_status`` can keep the connection open (``CONTINUE``), close it (``CLOSE``), or
  reset it (``CLOSE_RST``).

Failure handling
----------------

The filter is fail-closed by default: if the gRPC stream cannot be opened or fails, Envoy closes
the connection. Set :ref:`failure_mode_allow <envoy_v3_api_field_extensions.filters.network.ext_proc.v3.NetworkExternalProcessor.failure_mode_allow>`
to ``true`` to fail-open and continue forwarding data without external processing in those failure
cases.

Use :ref:`message_timeout <envoy_v3_api_field_extensions.filters.network.ext_proc.v3.NetworkExternalProcessor.message_timeout>`
to bound per-message synchronous processing latency (default: 200ms).

Metadata forwarding
-------------------

Use
:ref:`metadata_options.forwarding_namespaces <envoy_v3_api_field_extensions.filters.network.ext_proc.v3.MetadataOptions.forwarding_namespaces>`
to forward selected dynamic metadata namespaces to the external processor:

* ``untyped`` namespaces are sent as ``google.protobuf.Struct``.
* ``typed`` namespaces are sent as ``google.protobuf.Any``.

Example
-------

.. code-block:: yaml

  filter_chains:
  - filters:
    - name: envoy.filters.network.ext_proc
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.ext_proc.v3.NetworkExternalProcessor
        stat_prefix: tcp_ext_proc
        grpc_service:
          envoy_grpc:
            cluster_name: network-ext-proc
        processing_mode:
          process_read: STREAMED
          process_write: STREAMED
        message_timeout: 0.2s
        failure_mode_allow: false
        metadata_options:
          forwarding_namespaces:
            untyped:
            - envoy.filters.listener.proxy_protocol

  clusters:
  - name: network-ext-proc
    type: STATIC
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {}
    load_assignment:
      cluster_name: network-ext-proc
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 50051

Statistics
----------

This filter outputs counters in the ``network_ext_proc.<stat_prefix>.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: auto

  streams_started, Counter, Total number of gRPC streams started.
  stream_msgs_sent, Counter, Total number of messages sent to the external processor.
  stream_msgs_received, Counter, Total number of messages received from the external processor.
  read_data_sent, Counter, Number of read-direction data frames sent for processing.
  write_data_sent, Counter, Number of write-direction data frames sent for processing.
  read_data_injected, Counter, Number of read-direction frames replaced with modified data.
  write_data_injected, Counter, Number of write-direction frames replaced with modified data.
  empty_response_received, Counter, Number of empty responses received.
  spurious_msgs_received, Counter, Number of protocol-violating/unexpected responses.
  streams_closed, Counter, Number of streams cleanly closed.
  streams_grpc_error, Counter, Number of stream terminations due to gRPC errors.
  streams_grpc_close, Counter, Number of remote half-close events on the gRPC stream.
  connections_closed, Counter, Number of downstream connections closed by processor instruction.
  connections_reset, Counter, Number of downstream connections reset by processor instruction.
  stream_open_failures, Counter, Number of failures opening the gRPC stream.
  failure_mode_allowed, Counter, Number of failures ignored because ``failure_mode_allow`` is enabled.
  message_timeouts, Counter, Number of message processing timeouts.
