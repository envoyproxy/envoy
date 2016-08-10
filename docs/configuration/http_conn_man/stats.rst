.. _config_http_conn_man_stats:

Statistics
==========

Every connection manager has a statistics tree rooted at *http.<stat_prefix>.* with the following
statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Description
   downstream_cx_ssl_total, Counter, Description
   downstream_cx_http1_total, Counter, Description
   downstream_cx_http2_total, Counter, Description
   downstream_cx_destroy, Counter, Description
   downstream_cx_destroy_remote, Counter, Description
   downstream_cx_destroy_local, Counter, Description
   downstream_cx_destroy_active_rq, Counter, Description
   downstream_cx_destroy_local_active_rq, Counter, Description
   downstream_cx_destroy_remote_active_rq, Counter, Description
   downstream_cx_active, Gauge, Description
   downstream_cx_ssl_active, Gauge, Description
   downstream_cx_http1_active, Gauge, Description
   downstream_cx_http2_active, Gauge, Description
   downstream_cx_protocol_error, Counter, Description
   downstream_cx_length_ms, Timer, Description
   downstream_cx_rx_bytes_total, Counter, Description
   downstream_cx_rx_bytes_buffered, Gauge, Description
   downstream_cx_tx_bytes_total, Counter, Description
   downstream_cx_tx_bytes_buffered, Gauge, Description
   downstream_cx_drain_close, Counter, Description
   downstream_cx_idle_timeout, Counter, Description
   downstream_rq_total, Counter, Description
   downstream_rq_http1_total, Counter, Description
   downstream_rq_http2_total, Counter, Description
   downstream_rq_active, Gauge, Description
   downstream_rq_response_before_rq_complete, Counter, Description
   downstream_rq_rx_reset, Counter, Description
   downstream_rq_tx_reset, Counter, Description
   downstream_rq_non_relative_path, Counter, Description
   downstream_rq_2xx, Counter, Description
   downstream_rq_3xx, Counter, Description
   downstream_rq_4xx, Counter, Description
   downstream_rq_5xx, Counter, Description
   downstream_rq_time, Timer, Description
   failed_generate_uuid, Counter, Description

Per user agent statistics
-------------------------

FIXFIX
