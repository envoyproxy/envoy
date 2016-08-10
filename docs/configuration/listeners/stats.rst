.. _config_listener_stats:

Statistics
==========

Every listener has a statistics tree rooted at *listener.<port>.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Description
   downstream_cx_destroy, Counter, Description
   downstream_cx_active, Gauge, Description
   downstream_cx_length_ms, Timer, Description
   ssl.connection_error, Counter, Description
   ssl.handshake, Counter, Description
   ssl.no_certificate, Counter, Description
   ssl.fail_verify_san, Counter, Description
   ssl.fail_verify_cert_hash, Counter, Description
   ssl.cipher.<cipher>, Counter, Description
