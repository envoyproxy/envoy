When converting an `RFC 8441 <https://www.rfc-editor.org/rfc/rfc8441>`_ WebSocket extended
``CONNECT`` request to an HTTP/1.1 upgrade, Envoy now generates the ``sec-websocket-key`` required
by the HTTP/1.1 upstream if the request has no key. Existing keys are preserved. The key is added
before upstream protocol selection and remains on later HTTP/2 or HTTP/3 hops. Envoy does not yet
validate the upstream's ``sec-websocket-accept`` value against a generated key. This change can be
reverted by setting
``envoy.reloadable_features.websocket_synthesize_key_on_h2_downgrade`` to ``false``.
