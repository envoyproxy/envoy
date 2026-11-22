The HTTP/1.1 client codec now generates the ``sec-websocket-key`` required by
`RFC 6455 <https://www.rfc-editor.org/rfc/rfc6455>`_ for WebSocket upgrade requests that do not
carry one, such as requests converted from an `RFC 8441 <https://www.rfc-editor.org/rfc/rfc8441>`_
extended ``CONNECT``. For those requests the codec validates the upstream's ``upgrade``,
``connection`` and ``sec-websocket-accept`` response headers and strips the generated accept before
forwarding the response. An invalid ``101 Switching Protocols`` response is treated as an upstream
protocol error, resulting in a ``502`` that participates in configured retries and outlier
detection. Each upstream attempt generates a new key, while existing client-supplied keys are
forwarded without additional validation. This change can be reverted by setting
``envoy.reloadable_features.http1_synthesize_websocket_key`` to ``false``.
