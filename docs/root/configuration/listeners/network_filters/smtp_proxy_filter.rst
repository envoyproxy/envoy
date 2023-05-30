.. _config_network_filters_smtp_proxy:

SMTP proxy
==========

The SMTP proxy filter decodes the initial capabilities negotiation
between a SMTP client (downstream) and a SMTP server (upstream) to
determine if the session will upgrade to TLS. It terminates TLS and
then enters a "passthrough" mode like tcp_proxy.

* SMTP :ref:`architecture overview <arch_overview_smtp>`

.. attention::

   The ``smtp_proxy`` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.


Configuration
-------------

The SMTP proxy filter should be chained with TCP proxy and the
StartTLS transport socket as shown in the configuration example below:

.. code-block:: yaml

    filter_chains:
    - filters:
      - name: envoy.filters.network.smtp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.smtp_proxy.v3alpha.SmtpProxy
          stat_prefix: smtp
      - name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: smtp_cluster

      transport_socket:
        name: envoy.transport_sockets.starttls
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.starttls.v3.StartTlsConfig
          tls_socket_config:
            # "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:


This can also be used with upstream_proxy_protocol on the upstream
cluster to deliver the real client IP, etc, to an SMTP server that
supports proxy protocol.

.. _config_network_filters_smtp_proxy_stats:

Statistics
----------

Every configured SMTP proxy filter has statistics rooted at smtp.<stat_prefix> with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 2, 1, 2

  sessions, Counter,
  sessions_upstream_desync, Counter, protocol error: upstream spoke while waiting for downstream
  sessions_downstream_desync, Counter, protocol error: downstream spoke while waiting for upstream
  sessions_bad_line, Counter, protocol error: downstream sent (grossly) long command line
  sessions_bad_pipeline, Counter, protocol error: downstream pipelined additional data after a command line where it is not allowed
  sessions_bad_ehlo, Counter, downstream sent something other than HELO or EHLO for first command
  sessions_non_esmtp, Counter, downstream sent HELO instead of EHLO
  sessions_esmtp_unencrypted, Counter, downstream did not upgrade to TLS i.e. command following EHLO was not STARTTLS
  sessions_downstream_terminated_ssl, Counter, Envoy successfully terminated TLS initiated by downstream
  sessions_upstream_terminated_ssl, Counter, Envoy successfully initiated TLS with the upstream
  sessions_downstream_ssl_err, Counter, TLS handshake error with downstream
  sessions_upstream_ssl_err, Counter, TLS handshake error with upstream
  sessions_upstream_ssl_command_err, Counter, upstream returned an error response to STARTTLS SMTP command


.. _config_network_filters_smtp_proxy_dynamic_metadata:

Dynamic Metadata
----------------

TODO the SMTP filter currently does not emit Dynamic Metadata.
