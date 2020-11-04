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
   downstream_cx_upgrades_total, Counter, Total successfully upgraded connections. These are also counted as total http1/http2 connections.
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
   downstream_cx_upgrades_active, Gauge, Total active upgraded connections. These are also counted as active http1/http2 connections.
   downstream_cx_http2_active, Gauge, Total active HTTP/2 connections
   downstream_cx_protocol_error, Counter, Total protocol errors
   downstream_cx_length_ms, Histogram, Connection length milliseconds
   downstream_cx_rx_bytes_total, Counter, Total bytes received
   downstream_cx_rx_bytes_buffered, Gauge, Total received bytes currently buffered
   downstream_cx_tx_bytes_total, Counter, Total bytes sent
   downstream_cx_tx_bytes_buffered, Gauge, Total sent bytes currently buffered
   downstream_cx_drain_close, Counter, Total connections closed due to draining
   downstream_cx_idle_timeout, Counter, Total connections closed due to idle timeout
   downstream_cx_max_duration_reached, Counter, Total connections closed due to max connection duration
   downstream_cx_overload_disable_keepalive, Counter, Total connections for which HTTP 1.x keepalive has been disabled due to Envoy overload
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
   downstream_rq_ws_on_non_ws_route, Counter, Total upgrade requests rejected by non upgrade routes. This now applies both to WebSocket and non-WebSocket upgrades
   downstream_rq_time, Histogram, Total time for request and response (milliseconds)
   downstream_rq_idle_timeout, Counter, Total requests closed due to idle timeout
   downstream_rq_max_duration_reached, Counter, Total requests closed due to max duration reached
   downstream_rq_timeout, Counter, Total requests closed due to a timeout on the request path
   downstream_rq_overload_close, Counter, Total requests closed due to Envoy overload
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

Each codec has the option of adding per-codec statistics. Both http1 and http2 have codec stats.

Http1 codec statistics
~~~~~~~~~~~~~~~~~~~~~~

On the downstream side all http1 statistics are rooted at *http1.*

On the upstream side all http1 statistics are rooted at *cluster.<name>.http1.*

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   dropped_headers_with_underscores, Counter, Total number of dropped headers with names containing underscores. This action is configured by setting the :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`.
   metadata_not_supported_error, Counter, Total number of metadata dropped during HTTP/1 encoding
   response_flood, Counter, Total number of connections closed due to response flooding
   requests_rejected_with_underscores_in_headers, Counter, Total numbers of rejected requests due to header names containing underscores. This action is configured by setting the :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`.

Http2 codec statistics
~~~~~~~~~~~~~~~~~~~~~~

On the downstream side all http2 statistics are rooted at *http2.*

On the upstream side all http2 statistics are rooted at *cluster.<name>.http2.*

.. csv-table::
   :header: Name, Type, Description
   :widths: 1, 1, 2

   dropped_headers_with_underscores, Counter, Total number of dropped headers with names containing underscores. This action is configured by setting the :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`.
   header_overflow, Counter, Total number of connections reset due to the headers being larger than the :ref:`configured value <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.max_request_headers_kb>`.
   headers_cb_no_stream, Counter, Total number of errors where a header callback is called without an associated stream. This tracks an unexpected occurrence due to an as yet undiagnosed bug
   inbound_empty_frames_flood, Counter, Total number of connections terminated for exceeding the limit on consecutive inbound frames with an empty payload and no end stream flag. The limit is configured by setting the :ref:`max_consecutive_inbound_frames_with_empty_payload config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_consecutive_inbound_frames_with_empty_payload>`.
   inbound_priority_frames_flood, Counter, Total number of connections terminated for exceeding the limit on inbound frames of type PRIORITY. The limit is configured by setting the :ref:`max_inbound_priority_frames_per_stream config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_inbound_priority_frames_per_stream>`.
   inbound_window_update_frames_flood, Counter, Total number of connections terminated for exceeding the limit on inbound frames of type WINDOW_UPDATE. The limit is configured by setting the :ref:`max_inbound_window_updateframes_per_data_frame_sent config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_inbound_window_update_frames_per_data_frame_sent>`.
   outbound_flood, Counter, Total number of connections terminated for exceeding the limit on outbound frames of all types. The limit is configured by setting the :ref:`max_outbound_frames config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_outbound_frames>`.
   outbound_control_flood, Counter, "Total number of connections terminated for exceeding the limit on outbound frames of types PING, SETTINGS and RST_STREAM. The limit is configured by setting the :ref:`max_outbound_control_frames config setting <envoy_v3_api_field_config.core.v3.Http2ProtocolOptions.max_outbound_control_frames>`."
   requests_rejected_with_underscores_in_headers, Counter, Total numbers of rejected requests due to header names containing underscores. This action is configured by setting the :ref:`headers_with_underscores_action config setting <envoy_v3_api_field_config.core.v3.HttpProtocolOptions.headers_with_underscores_action>`.
   rx_messaging_error, Counter, Total number of invalid received frames that violated `section 8 <https://tools.ietf.org/html/rfc7540#section-8>`_ of the HTTP/2 spec. This will result in a *tx_reset*
   rx_reset, Counter, Total number of reset stream frames received by Envoy
   trailers, Counter, Total number of trailers seen on requests coming from downstream
   tx_flush_timeout, Counter, Total number of :ref:`stream idle timeouts <envoy_api_field_config.filter.network.http_connection_manager.v2.HttpConnectionManager.stream_idle_timeout>` waiting for open stream window to flush the remainder of a stream
   tx_reset, Counter, Total number of reset stream frames transmitted by Envoy
   keepalive_timeout, Counter, Total number of connections closed due to :ref:`keepalive timeout <envoy_v3_api_field_config.core.v3.KeepaliveSettings.timeout>`
   streams_active, Gauge, Active streams as observed by the codec
   pending_send_bytes, Gauge, Currently buffered body data in bytes waiting to be written when stream/connection window is opened.

.. attention::

  The HTTP/2 `streams_active` gauge may be greater than the HTTP connection manager
  `downstream_rq_active` gauge due to differences in stream accounting between the codec and the
  HTTP connection manager.

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
