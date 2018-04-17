.. _config_listener_stats:

Statistics
==========

Listener
--------

Every listener has a statistics tree rooted at *listener.<address>.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Total connections
   downstream_cx_destroy, Counter, Total destroyed connections
   downstream_cx_active, Gauge, Total active connections
   downstream_cx_length_ms, Histogram, Connection length milliseconds
   ssl.connection_error, Counter, Total TLS connection errors not including failed certificate verifications
   ssl.handshake, Counter, Total successful TLS connection handshakes
   ssl.session_reused, Counter, Total successful TLS session resumptions
   ssl.no_certificate, Counter, Total successul TLS connections with no client certificate
   ssl.fail_no_sni_match, Counter, Total TLS connections that were rejected because of missing SNI match
   ssl.fail_verify_no_cert, Counter, Total TLS connections that failed because of missing client certificate
   ssl.fail_verify_error, Counter, Total TLS connections that failed CA verification
   ssl.fail_verify_san, Counter, Total TLS connections that failed SAN verification
   ssl.fail_verify_cert_hash, Counter, Total TLS connections that failed certificate pinning verification
   ssl.cipher.<cipher>, Counter, Total TLS connections that used <cipher>

Listener manager
----------------

The listener manager has a statistics tree rooted at *listener_manager.* with the following
statistics. Any ``:`` character in the stats name is replaced with ``_``.

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   listener_added, Counter, Total listeners added (either via static config or LDS)
   listener_modified, Counter, Total listeners modified (via LDS)
   listener_removed, Counter, Total listeners removed (via LDS)
   listener_create_success, Counter, Total listener objects successfully added to workers
   listener_create_failure, Counter, Total failed listener object additions to workers
   total_listeners_warming, Gauge, Number of currently warming listeners
   total_listeners_active, Gauge, Number of currently active listeners
   total_listeners_draining, Gauge, Number of currently draining listeners
