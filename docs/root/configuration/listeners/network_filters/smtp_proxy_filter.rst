.. config_network_filters_smtp_proxy:

SMTP Proxy
==========

* SMTP Proxy :ref:`architecture overview <arch_overview_smtp>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.smtp_proxy.v3alpha.SmtpProxy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.smtp_proxy.v3alpha.SmtpProxy>`

Configuration
-------------

The SMTP proxy filter should be chained with the TCP proxy as shown in the configuration
example below:

.. code-block:: yaml

    filter_chains:
    - filters:
      - name: envoy.filters.network.smtp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.smtp_proxy.v3alpha.SmtpProxy
          stat_prefix: smtp_stats
          upstream_tls: REQUIRE
      - name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp
          cluster: smtp_cluster

.. _config_network_filters_smtp_proxy_stats:

Statistics
----------

Every configured SMTP proxy filter has statistics rooted at *smtp.<stat_prefix>.* with the
following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 2, 1, 2

  smtp_session_requests, Counter, Number of SMTP connection requests
  smtp_connection_establishment_errors, Counter, Number of SMTP connection establishment errors
  smtp_sessions_completed, Counter, Number of successful SMTP sessions
  smtp_sessions_terminated, Counter, Number of SMTP sessions terminated prematurely by filter due to an error
  smtp_transactions, Counter, Total Number of successful SMTP transactions
  smtp_transactions_aborted, Counter, Number of SMTP transactions aborted without completion
  smtp_tls_terminated_sessions, Counter, Number of SMTP sessions with successful Downstream TLS termination
  smtp_tls_termination_errors, Counter, Number of Downstream TLS termination i.e. STARTTLS handling error
  smtp_auth_errors, Counter, Number of AUTH errors received from upstream
  smtp_mail_data_transfer_errors, Counter, Number of errors received from upstream for DATA command
  smtp_rcpt_errors, Counter,  Number of errors received from upstream for DATA command
  sessions_upstream_tls_failed, Counter, Number of SMTP sessions errors with Upstream TLS conversion, i.e upstream STARTTLS error
  sessions_upstream_tls_success, Counter, Number of SMTP sessions with successful upstream STARTTLS.
