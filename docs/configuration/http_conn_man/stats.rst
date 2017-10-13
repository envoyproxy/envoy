.. _config_http_conn_man_stats:

Statistics
==========

Every connection manager has a statistics tree rooted at *http.<stat_prefix>.* with the following
statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Total connections
   downstream_cx_ssl_total, Counter, Total TLS connections
   downstream_cx_http1_total, Counter, Total HTTP/1.1 connections
   downstream_cx_websocket_total, Counter, Total WebSocket connections
   downstream_cx_http2_total, Counter, Total HTTP/2 connections
   downstream_cx_destroy, Counter, Total connections destroyed
   downstream_cx_destroy_remote, Counter, Total connections destroyed due to remote close
   downstream_cx_destroy_local, Counter, Total connections destroyed due to local close
   downstream_cx_destroy_active_rq, Counter, Total connections destroyed with 1+ active request
   downstream_cx_destroy_local_active_rq, Counter, Total connections destroyed locally with 1+ active request
   downstream_cx_destroy_remote_active_rq, Counter, Total connections destroyed remotely with 1+ active request
   downstream_cx_active, Gauge, Total active connections
   downstream_cx_ssl_active, Gauge, Total active TLS connections
   downstream_cx_http1_active, Gauge, Total active HTTP/1.1 connections
   downstream_cx_websocket_active, Gauge, Total active WebSocket connections
   downstream_cx_http2_active, Gauge, Total active HTTP/2 connections
   downstream_cx_protocol_error, Counter, Total protocol errors
   downstream_cx_length_ms, Histogram, Connection length milliseconds
   downstream_cx_rx_bytes_total, Counter, Total bytes received
   downstream_cx_rx_bytes_buffered, Gauge, Total received bytes currently buffered
   downstream_cx_tx_bytes_total, Counter, Total bytes sent
   downstream_cx_tx_bytes_buffered, Gauge, Total sent bytes currently buffered
   downstream_cx_drain_close, Counter, Total connections closed due to draining
   downstream_cx_idle_timeout, Counter, Total connections closed due to idle timeout
   downstream_flow_control_paused_reading_total, Counter, Total number of times reads were disabled due to flow control
   downstream_flow_control_resumed_reading_total, Counter, Total number of times reads were enabled on the connection due to flow control
   downstream_rq_total, Counter, Total requests
   downstream_rq_http1_total, Counter, Total HTTP/1.1 requests
   downstream_rq_http2_total, Counter, Total HTTP/2 requests
   downstream_rq_active, Gauge, Total active requests
   downstream_rq_response_before_rq_complete, Counter, Total responses sent before the request was complete
   downstream_rq_rx_reset, Counter, Total request resets received
   downstream_rq_tx_reset, Counter, Total request resets sent
   downstream_rq_non_relative_path, Counter, Total requests with a non-relative HTTP path
   downstream_rq_too_large, Counter, Total requests resulting in a 413 due to buffering an overly large body.
   downstream_rq_2xx, Counter, Total 2xx responses
   downstream_rq_3xx, Counter, Total 3xx responses
   downstream_rq_4xx, Counter, Total 4xx responses
   downstream_rq_5xx, Counter, Total 5xx responses
   downstream_rq_ws_on_non_ws_route, Counter, Total WebSocket upgrade requests rejected by non WebSocket routes
   downstream_rq_time, Histogram, Request time milliseconds
   rs_too_large, Counter, Total response errors due to buffering an overly large body.

Per user agent statistics
-------------------------

Additional per user agent statistics are rooted at *http.<stat_prefix>.user_agent.<user_agent>.*
Currently Envoy matches user agent for both iOS (*ios*) and Android (*android*) and produces
the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_cx_total, Counter, Total connections
   downstream_cx_destroy_remote_active_rq, Counter, Total connections destroyed remotely with 1+ active requests
   downstream_rq_total, Counter, Total requests


Per listener statistics
-----------------------

Additional per listener statistics are rooted at *listener.<address>.http.<stat_prefix>.* with the
following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_rq_2xx, Counter, Total 2xx responses
   downstream_rq_3xx, Counter, Total 3xx responses
   downstream_rq_4xx, Counter, Total 4xx responses
   downstream_rq_5xx, Counter, Total 5xx responses
