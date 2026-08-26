When converting an `RFC 8441 <https://www.rfc-editor.org/rfc/rfc8441>`_ WebSocket extended
``CONNECT`` request to an HTTP/1.1 upgrade, Envoy now generates the ``sec-websocket-key`` required
by that upstream attempt if the request has no key, and validates the upstream's
``sec-websocket-accept`` response. An upstream that answers ``101 Switching Protocols`` without
echoing the matching accept is now rejected with a ``502`` and the
``websocket_handshake_invalid_accept`` response code detail. Retries generate new keys, while
existing client-supplied keys are preserved without additional validation. This change can be
reverted by setting
``envoy.reloadable_features.websocket_synthesize_key_on_h2_downgrade`` to ``false``.
