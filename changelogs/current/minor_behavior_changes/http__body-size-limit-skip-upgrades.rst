The :ref:`body size limit filter <config_http_filters_body_size_limit>` no longer counts bytes sent
after the upstream accepts an upgrade such as WebSocket, or a CONNECT request. Those bytes are
tunneled traffic rather than a request body, and counting them reset long-lived WebSocket
connections once their total traffic passed the limit. Bytes sent before the upstream accepts are
still counted, and so is CONNECT-UDP that Envoy terminates itself.
