Updated Envoy's legacy nghttp2 HTTP/2 codec for nghttp2 >= 1.67.0 connection-level messaging and
flow-control violations. Envoy now preserves ``http2.rx_messaging_error`` and HTTP/2 response-code
details such as ``http2.violation.of.messaging.rule`` when nghttp2 terminates the connection with
``GOAWAY`` instead of delivering ``on_invalid_frame_recv_callback``. Because nghttp2 has already
committed to ``GOAWAY`` in these paths, ``override_stream_error_on_invalid_http_message`` still
cannot convert them back into stream errors.
