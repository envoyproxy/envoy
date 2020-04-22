.. _operations_traffic_tapping:

Traffic tapping
===============

Envoy currently provides two experimental extensions that can tap traffic:

  * :ref:`HTTP tap filter <config_http_filters_tap>`. See the linked filter documentation for more
    information.
  * :ref:`Tap transport socket extension <envoy_api_msg_core.TransportSocket>` that can intercept
    traffic and write to a :ref:`protobuf trace file
    <envoy_api_msg_data.tap.v2alpha.TraceWrapper>`. The remainder of this document describes
    the configuration of the tap transport socket.

Tap transport socket configuration
----------------------------------

.. attention::

  The tap transport socket is experimental and is currently under active development. There is
  currently a very limited set of match conditions, output configuration, output sinks, etc.
  Capabilities will be expanded over time and the configuration structures are likely to change.

Tapping can be configured on :ref:`Listener
<envoy_api_field_listener.FilterChain.transport_socket>` and :ref:`Cluster
<envoy_api_field_Cluster.transport_socket>` transport sockets, providing the ability to interpose on
downstream and upstream L4 connections respectively.

To configure traffic tapping, add an `envoy.transport_sockets.tap` transport socket
:ref:`configuration <envoy_api_msg_config.transport_socket.tap.v2alpha.Tap>` to the listener
or cluster. For a plain text socket this might look like:

.. code-block:: yaml

  transport_socket:
    name: envoy.transport_sockets.tap
    typed_config:
      "@type": type.googleapis.com/envoy.config.transport_socket.tap.v2alpha.Tap
      common_config:
        static_config:
          match_config:
            any_match: true
          output_config:
            sinks:
              - format: PROTO_BINARY
                file_per_tap:
                  path_prefix: /some/tap/path
      transport_socket:
        name: envoy.transport_sockets.raw_buffer

For a TLS socket, this will be:

.. code-block:: yaml

  transport_socket:
    name: envoy.transport_sockets.tap
    typed_config:
      "@type": type.googleapis.com/envoy.config.transport_socket.tap.v2alpha.Tap
      common_config:
        static_config:
          match_config:
            any_match: true
          output_config:
            sinks:
              - format: PROTO_BINARY
                file_per_tap:
                  path_prefix: /some/tap/path
      transport_socket:
        name: envoy.transport_sockets.tls
        typed_config: <TLS context>

where the TLS context configuration replaces any existing :ref:`downstream
<envoy_api_msg_auth.DownstreamTlsContext>` or :ref:`upstream
<envoy_api_msg_auth.UpstreamTlsContext>`
TLS configuration on the listener or cluster, respectively.

Each unique socket instance will generate a trace file prefixed with `path_prefix`. E.g.
`/some/tap/path_0.pb`.

Buffered data limits
--------------------

For buffered socket taps, Envoy will limit the amount of body data that is tapped to avoid OOM
situations. The default limit is 1KiB for both received and transmitted data.
This is configurable via the :ref:`max_buffered_rx_bytes
<envoy_api_field_service.tap.v2alpha.OutputConfig.max_buffered_rx_bytes>` and
:ref:`max_buffered_tx_bytes
<envoy_api_field_service.tap.v2alpha.OutputConfig.max_buffered_tx_bytes>` settings. When a buffered
socket tap is truncated, the trace will indicate truncation via the :ref:`read_truncated
<envoy_api_field_data.tap.v2alpha.SocketBufferedTrace.read_truncated>` and :ref:`write_truncated
<envoy_api_field_data.tap.v2alpha.SocketBufferedTrace.write_truncated>` fields as well as the body
:ref:`truncated <envoy_api_field_data.tap.v2alpha.Body.truncated>` field.

Streaming
---------

The tap transport socket supports both buffered and streaming, controlled by the :ref:`streaming
<envoy_api_field_service.tap.v2alpha.OutputConfig.streaming>` setting. When buffering,
:ref:`SocketBufferedTrace <envoy_api_msg_data.tap.v2alpha.SocketBufferedTrace>` messages are
emitted. When streaming, a series of :ref:`SocketStreamedTraceSegment
<envoy_api_msg_data.tap.v2alpha.SocketStreamedTraceSegment>` are emitted.

See the :ref:`HTTP tap filter streaming <config_http_filters_tap_streaming>` documentation for more
information. Most of the concepts overlap between the HTTP filter and the transport socket.

PCAP generation
---------------

The generated trace file can be converted to `libpcap format
<https://wiki.wireshark.org/Development/LibpcapFileFormat>`_, suitable for
analysis with tools such as `Wireshark <https://www.wireshark.org/>`_ with the
`tap2pcap` utility, e.g.:

.. code-block:: bash

  bazel run @envoy_api_canonical//tools:tap2pcap /some/tap/path_0.pb path_0.pcap
  tshark -r path_0.pcap -d "tcp.port==10000,http2" -P
    1   0.000000    127.0.0.1 → 127.0.0.1    HTTP2 157 Magic, SETTINGS, WINDOW_UPDATE, HEADERS
    2   0.013713    127.0.0.1 → 127.0.0.1    HTTP2 91 SETTINGS, SETTINGS, WINDOW_UPDATE
    3   0.013820    127.0.0.1 → 127.0.0.1    HTTP2 63 SETTINGS
    4   0.128649    127.0.0.1 → 127.0.0.1    HTTP2 5586 HEADERS
    5   0.130006    127.0.0.1 → 127.0.0.1    HTTP2 7573 DATA
    6   0.131044    127.0.0.1 → 127.0.0.1    HTTP2 3152 DATA, DATA
