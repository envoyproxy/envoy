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
   downstream_pre_cx_timeout, Counter, Sockets that timed out during listener filter processing
   downstream_pre_cx_active, Gauge, Sockets currently undergoing listener filter processing
   no_filter_chain_match, Counter, Total connections that didn't match any filter chain
   ssl.connection_error, Counter, Total TLS connection errors not including failed certificate verifications
   ssl.handshake, Counter, Total successful TLS connection handshakes
   ssl.session_reused, Counter, Total successful TLS session resumptions
   ssl.no_certificate, Counter, Total successful TLS connections with no client certificate
   ssl.fail_verify_no_cert, Counter, Total TLS connections that failed because of missing client certificate
   ssl.fail_verify_error, Counter, Total TLS connections that failed CA verification
   ssl.fail_verify_san, Counter, Total TLS connections that failed SAN verification
   ssl.fail_verify_cert_hash, Counter, Total TLS connections that failed certificate pinning verification
   ssl.ciphers.<cipher>, Counter, Total successful TLS connections that used cipher <cipher>
   ssl.curves.<curve>, Counter, Total successful TLS connections that used ECDHE curve <curve>
   ssl.sigalgs.<sigalg>, Counter, Total successful TLS connections that used signature algorithm <sigalg>
   ssl.versions.<version>, Counter, Total successful TLS connections that used protocol version <version>

.. _config_listener_stats_per_handler:

Per-handler Listener Stats
--------------------------

Every listener additionally has a statistics tree rooted at *listener.<address>.<handler>.* which
contains *per-handler* statistics. As described in the
:ref:`threading model <arch_overview_threading>` documentation, Envoy has a threading model which
includes the *main thread* as well as a number of *worker threads* which are controlled by the
:option:`--concurrency` option. Along these lines, *<handler>* is equal to *main_thread*,
*worker_0*, *worker_1*, etc. These statistics can be used to look for per-handler/worker imbalance
on either accepted or active connections.

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Total connections on this handler.
   downstream_cx_active, Gauge, Total active connections on this handler.

.. _config_listener_manager_stats:

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
   listener_stopped, Counter, Total listeners stopped
   listener_create_success, Counter, Total listener objects successfully added to workers
   listener_create_failure, Counter, Total failed listener object additions to workers
   total_listeners_warming, Gauge, Number of currently warming listeners
   total_listeners_active, Gauge, Number of currently active listeners
   total_listeners_draining, Gauge, Number of currently draining listeners
   workers_started, Gauge, A boolean (1 if started and 0 otherwise) that indicates whether listeners have been initialized on workers.
