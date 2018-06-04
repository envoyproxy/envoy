.. _operations_traffic_capture:

Traffic capture
===============

Envoy currently provides an experimental :ref:`transport socket extension
<envoy_api_msg_core.TransportSocket>` that can intercept traffic and write to a :ref:`protobuf
capture file <envoy_api_msg_data.tap.v2alpha.Trace>`.

.. warning::
  This feature is experimental and has a known limitation that it will OOM for large traces on a
  given socket. It can also be disabled in the build if there are security concerns, see
  https://github.com/envoyproxy/envoy/blob/master/bazel/README.md#disabling-extensions.

Configuration
-------------

Capture can be configured on :ref:`Listener
<envoy_api_field_listener.FilterChain.transport_socket>` and :ref:`Cluster
<envoy_api_field_Cluster.transport_socket>` transport sockets, providing the ability to interpose on
downstream and upstream L4 connections respectively.

To configure traffic capture, add an `envoy.transport_sockets.capture` transport socket
:ref:`configuration <envoy_api_msg_config.transport_socket.capture.v2alpha.Capture>` to the listener
or cluster. For a plain text socket this might look like:

.. code-block:: yaml

  transport_socket:
    name: envoy.transport_sockets.capture
    config:
      file_sink:
        path_prefix: /some/capture/path
      transport_socket:
        name: raw_buffer

For a TLS socket, this will be:

.. code-block:: yaml

  transport_socket:
    name: envoy.transport_sockets.capture
    config:
      file_sink:
        path_prefix: /some/capture/path
      transport_socket:
        name: ssl
        config: <TLS context>

where the TLS context configuration replaces any existing :ref:`downstream
<envoy_api_msg_auth.DownstreamTlsContext>` or :ref:`upstream
<envoy_api_msg_auth.UpstreamTlsContext>`
TLS configuration on the listener or cluster, respectively.

Each unique socket instance will generate a trace file prefixed with `path_prefix`. E.g.
`/some/capture/path_0.pb`.

PCAP generation
---------------

The generated trace file can be converted to `libpcap format
<https://wiki.wireshark.org/Development/LibpcapFileFormat>`_, suitable for
analysis with tools such as `Wireshark <https://www.wireshark.org/>`_ with the
`capture2pcap` utility, e.g.:

.. code-block:: bash

  bazel run @envoy_api//tools:capture2pcap /some/capture/path_0.pb path_0.pcap
  tshark -r path_0.pcap -d "tcp.port==10000,http2" -P
    1   0.000000    127.0.0.1 → 127.0.0.1    HTTP2 157 Magic, SETTINGS, WINDOW_UPDATE, HEADERS
    2   0.013713    127.0.0.1 → 127.0.0.1    HTTP2 91 SETTINGS, SETTINGS, WINDOW_UPDATE
    3   0.013820    127.0.0.1 → 127.0.0.1    HTTP2 63 SETTINGS
    4   0.128649    127.0.0.1 → 127.0.0.1    HTTP2 5586 HEADERS
    5   0.130006    127.0.0.1 → 127.0.0.1    HTTP2 7573 DATA
    6   0.131044    127.0.0.1 → 127.0.0.1    HTTP2 3152 DATA, DATA
