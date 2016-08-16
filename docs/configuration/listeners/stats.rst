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
   ssl.connection_error, Counter, Total SSL connection errors
   ssl.handshake, Counter, Total SSL connection handshakes
   ssl.no_certificate, Counter, Total SSL connections with no client certificate
   ssl.fail_verify_san, Counter, Total SSL connections that failed SAN verification
   ssl.fail_verify_cert_hash, Counter, Total SSL connections that failed certificate pinning verification
   ssl.cipher.<cipher>, Counter, Total SSL connections that used <cipher>
