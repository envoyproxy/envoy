.. _config_listener_stats:

Statistics
==========

Listener
--------

Every listener has a statistics tree rooted at *listener.<address>.* (or *listener.<stat_prefix>.* if
:ref:`stat_prefix <envoy_v3_api_field_config.listener.v3.Listener.stat_prefix>` is non-empty)
with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Total connections
   downstream_cx_destroy, Counter, Total destroyed connections
   downstream_cx_active, Gauge, Total active connections
   downstream_cx_length_ms, Histogram, Connection length milliseconds
   downstream_cx_transport_socket_connect_timeout, Counter, Total connections that timed out during transport socket connection negotiation
   downstream_cx_overflow, Counter, Total connections rejected due to enforcement of listener connection limit
   downstream_cx_overload_reject, Counter, Total connections rejected due to configured overload actions
   downstream_global_cx_overflow, Counter, Total connections rejected due to enforcement of global connection limit
   connections_accepted_per_socket_event, Histogram, Number of connections accepted per listener socket event
   downstream_pre_cx_timeout, Counter, Sockets that timed out during listener filter processing
   downstream_pre_cx_active, Gauge, Sockets currently undergoing listener filter processing
   extension_config_missing, Counter, Total connections closed due to missing listener filter extension configuration
   network_extension_config_missing, Counter, Total connections closed due to missing network filter extension configuration
   global_cx_overflow, Counter, Total connections rejected due to enforcement of the global connection limit
   no_filter_chain_match, Counter, Total connections that didn't match any filter chain
   downstream_listener_filter_remote_close, Counter, Total connections closed by remote when peek data for listener filters
   downstream_listener_filter_error, Counter, Total numbers of read errors when peeking data for listener filters

.. _config_listener_stats_tls:

TLS statistics
--------------

The following TLS statistics are rooted at *listener.<address>.ssl.*:

.. include:: ../../_include/ssl_stats.rst

.. _config_listener_stats_tcp:

TCP statistics
--------------

The following TCP statistics, which are available when using the :ref:`TCP stats transport socket <envoy_v3_api_msg_extensions.transport_sockets.tcp_stats.v3.Config>`,
are rooted at *listener.<address>.tcp_stats.*:

.. include:: ../../_include/tcp_stats.rst

.. _config_listener_stats_udp:

UDP statistics
--------------

The following UDP statistics are available for UDP listeners and are rooted at
*listener.<address>.udp.*:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_rx_datagram_dropped, Counter, Number of datagrams dropped due to kernel overflow or truncation

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

   listener_added, Counter, Total listeners added (either via static config or LDS).
   listener_modified, Counter, Total listeners modified (via LDS).
   listener_removed, Counter, Total listeners removed (via LDS).
   listener_stopped, Counter, Total listeners stopped.
   listener_create_success, Counter, Total listener objects successfully added to workers.
   listener_create_failure, Counter, Total failed listener object additions to workers.
   listener_in_place_updated, Counter, Total listener objects created to execute filter chain update path.
   total_filter_chains_draining, Gauge, Number of currently draining filter chains.
   total_listeners_warming, Gauge, Number of currently warming listeners.
   total_listeners_active, Gauge, Number of currently active listeners.
   total_listeners_draining, Gauge, Number of currently draining listeners.
   workers_started, Gauge, A boolean (1 if started and 0 otherwise) that indicates whether listeners have been initialized on workers.
