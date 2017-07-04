.. _config_listener_stats:

Statistics
==========

Every listener has a statistics tree rooted at *listener.<port>.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Total connections
   downstream_cx_destroy, Counter, Total destroyed connections
   downstream_cx_active, Gauge, Total active connections
   downstream_cx_length_ms, Timer, Connection length milliseconds
   ssl.connection_error, Counter, Total TLS connection errors
   ssl.handshake, Counter, Total successful TLS connection handshakes
   ssl.no_certificate, Counter, Total successul TLS connections with no client certificate
   ssl.fail_verify_no_cert, Counter, Total TLS connections that failed because of missing client certificate
   ssl.fail_verify_error, Counter, Total TLS connections that failed CA verification
   ssl.fail_verify_san, Counter, Total TLS connections that failed SAN verification
   ssl.fail_verify_cert_hash, Counter, Total TLS connections that failed certificate pinning verification
   ssl.cipher.<cipher>, Counter, Total TLS connections that used <cipher>
