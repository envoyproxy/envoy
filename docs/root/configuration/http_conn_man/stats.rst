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
   downstream_cx_upgrades_total, Counter, Total successfully upgraded connections
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
   downstream_cx_upgrades_active, Gauge, Total active upgraded connections
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
   downstream_cx_overload_disable_keepalive, Counter, Total connections for which HTTP 1.x keepalive has been disabled due to envoy overload
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
   downstream_rq_too_large, Counter, Total requests resulting in a 413 due to buffering an overly large body
   downstream_rq_completed, Counter, Total requests that resulted in a response (e.g. does not include aborted requests)
   downstream_rq_1xx, Counter, Total 1xx responses
   downstream_rq_2xx, Counter, Total 2xx responses
   downstream_rq_3xx, Counter, Total 3xx responses
   downstream_rq_4xx, Counter, Total 4xx responses
   downstream_rq_5xx, Counter, Total 5xx responses
   downstream_rq_ws_on_non_ws_route, Counter, Total WebSocket upgrade requests rejected by non WebSocket routes
   downstream_rq_time, Histogram, Request time milliseconds
   downstream_rq_idle_timeout, Counter, Total requests closed due to idle timeout
   downstream_rq_overload_close, Counter, Total requests closed due to envoy overload
   rs_too_large, Counter, Total response errors due to buffering an overly large body

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

.. _config_http_conn_man_stats_per_listener:

Per listener statistics
-----------------------

Additional per listener statistics are rooted at *listener.<address>.http.<stat_prefix>.* with the
following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   downstream_rq_completed, Counter, Total responses
   downstream_rq_1xx, Counter, Total 1xx responses
   downstream_rq_2xx, Counter, Total 2xx responses
   downstream_rq_3xx, Counter, Total 3xx responses
   downstream_rq_4xx, Counter, Total 4xx responses
   downstream_rq_5xx, Counter, Total 5xx responses

.. _config_http_conn_man_stats_per_codec:

Per codec statistics
-----------------------

Each codec has the option of adding per-codec statistics. Currently only http2 has codec stats.

Http2 codec statistics
~~~~~~~~~~~~~~~~~~~~~~

All http2 statistics are rooted at *http2.*

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   header_overflow, Counter, Total number of connections reset due to the headers being larger than `Envoy::Http::Http2::ConnectionImpl::StreamImpl::MAX_HEADER_SIZE` (63k)
   headers_cb_no_stream, Counter, Total number of errors where a header callback is called without an associated stream. This tracks an unexpected occurrence due to an as yet undiagnosed bug
   rx_messaging_error, Counter, Total number of invalid received frames that violated `section 8 <https://tools.ietf.org/html/rfc7540#section-8>`_ of the HTTP/2 spec. This will result in a *tx_reset*
   rx_reset, Counter, Total number of reset stream frames received by Envoy
   too_many_header_frames, Counter, Total number of times an HTTP2 connection is reset due to receiving too many headers frames. Envoy currently supports proxying at most one header frame for 100-Continue one non-100 response code header frame and one frame with trailers
   trailers, Counter, Total number of trailers seen on requests coming from downstream
   tx_reset, Counter, Total number of reset stream frames transmitted by Envoy

Tracing statistics
------------------

Tracing statistics are emitted when tracing decisions are made. All tracing statistics are rooted at *http.<stat_prefix>.tracing.* with the following statistics:

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   random_sampling, Counter, Total number of traceable decisions by random sampling
   service_forced, Counter, Total number of traceable decisions by server runtime flag *tracing.global_enabled*
   client_enabled, Counter, Total number of traceable decisions by request header *x-envoy-force-trace*
   not_traceable, Counter, Total number of non-traceable decisions by request id
   health_check, Counter, Total number of non-traceable decisions by health check
