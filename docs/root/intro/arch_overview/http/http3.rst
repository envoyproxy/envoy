.. _arch_overview_http3:

HTTP3 overview
==============

.. warning::

  HTTP/3 support is still in Alpha, and should be used with caution.
  Outstanding issues required for HTTP/3 to go GA can be found
  `here <https://github.com/envoyproxy/envoy/labels/quic-mvp>`_
  For example QUIC does not currently support in-place filter chain updates, so users
  requiring dynamic config reload for QUIC should wait until
  `#13115 <https://github.com/envoyproxy/envoy/issues/13115>`_ has been addressed.

  For general feature requests beyond production readiness, you can track
  the `area-quic <https://github.com/envoyproxy/envoy/labels/area%2Fquic>`_ tag.

HTTP3 downstream
----------------

Downstream Envoy HTTP/3 support can be turned up via adding
:ref:`quic_options <envoy_v3_api_field_config.listener.v3.UdpListenerConfig.quic_options>`,
ensuring the downstream transport socket is a QuicDownstreamTransport, and setting the codec
to HTTP/3.

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

HTTP3 upstream
--------------

HTTP/3 upstream support is still in Alpha, and should be used with caution.
Outstanding issues required for HTTP/3 to go GA can be found
`here <https://github.com/envoyproxy/envoy/labels/quic-mvp>`_

Envoy HTTP/3 support can be turned up by turning up HTTP/3 support in
:ref:`http_protocol_options <envoy_v3_api_msg_extensions.upstreams.http.v3.HttpProtocolOptions>`,
Either configuring HTTP/3 explicitly on, or using the auto_http option to use HTTP/3 if it is supported.

See :ref:`here <arch_overview_http3_upstream>` for more information about HTTP/3 connection pooling, including
detailed information of where QUIC will be used, and how it fails over to TCP when QUIC use is configured to be optional.

An example upstream HTTP/3 configuration file can be found :repo:`here </configs/google_com_http3_upstream_proxy.yaml>`.
