.. _arch_overview_http3:

HTTP3 overview
==============

.. warning::

  HTTP/3 downstream support is ready for production use, but continued improvements are coming,
  tracked in the `area-quic <https://github.com/envoyproxy/envoy/labels/area%2Fquic>`_ tag.

  HTTP/3 upstream support is fine for locally controlled networks, but is not ready for
  general internet use, and is missing some key latency features. See details below.


HTTP3 downstream
----------------

Downstream Envoy HTTP/3 support can be turned up via adding
:ref:`quic_options <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.quic_options>`,
ensuring the downstream transport socket is a QuicDownstreamTransport, and setting the codec
to HTTP/3. Please note that hot restart is not gracefully handled for HTTP/3 yet.

See example :repo:`downstream HTTP/3 configuration </configs/envoyproxy_io_proxy_http3_downstream.yaml>` for example configuration.

Note that the example configuration includes both a TCP and a UDP listener, and the TCP
listener is advertising http/3 support via an ``alt-svc header``. Advertising HTTP/3 is not necessary for
in-house deployments where HTTP/3 is explicitly configured, but is needed for internet facing deployments
where TCP is the default, and clients such as Chrome will only attempt HTTP/3 if it is explicitly advertised.

By default the example configuration uses kernel UDP support, but for production performance use of
BPF is strongly advised if Envoy is running with multiple worker threads. Envoy will attempt to
use BPF on Linux by default if multiple worker threads are configured, but may require root, or at least
sudo-with-permissions (e.g. sudo setcap cap_bpf+ep). If multiple worker threads are configured, Envoy will
log a warning on start-up if BPF is unsupported on the platform, or is attempted and fails.

It is recommanded to monitor some UDP listener and QUIC connection stats:
* :repo:`UDP listener downstream_rx_datagram_dropped </docs/root/configuration/listeners/stats.rst#udp-statistics>`: non-zero means kernel's UDP listen socket's receive buffer isn't large enough. In Linux, it can be configured via listener :ref:`socket_options <envoy_v3_api_field_config.listener.v3.Listener.socket_options>` by setting prebinding socket option SO_RCVBUF at SOL_SOCKET level.
* :repo:`QUIC connection error codes and stream reset error codes </docs/root/configuration/http/http_conn_man/stats.rst#http3-per-listener-statistics>`: please refer to `quic_error_codes.h <https://github.com/google/quiche/blob/main/quiche/quic/core/quic_error_codes.h>`_ for the meaning of each error code.

HTTP3 upstream
--------------

HTTP/3 upstream support is implemented, with support both for explicit HTTP/3 (for data center use) and
automatic HTTP/3 (for internet use). If you are in a controlled environment where UDP is unlikely to be blocked,
you can configure it as the explicit protocol in
:ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>`. For internet use,
configuring auto_http with http3_protocol_options will result in Envoy attempting to use HTTP/3 for endpoints which
have explicitly advertised HTTP/3 support via an ``alt-svc header``. Envoy will attempt to create a QUIC connection,
then if the QUIC handshake is not complete after a short delay, will kick off a TCP connection, and will use whichever
is established first.

See :ref:`here <arch_overview_http3_upstream>` for more information about HTTP/3 connection pooling, including
detailed information of where QUIC will be used, and how it fails over to TCP when QUIC use is configured to be optional.

An example upstream HTTP/3 configuration file can be found :repo:`here </configs/google_com_http3_upstream_proxy.yaml>`.
