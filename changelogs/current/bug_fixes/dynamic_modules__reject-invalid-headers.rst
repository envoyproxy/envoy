Fixed the dynamic module HTTP filter writing header names and values from a module into Envoy's
header maps without checking them. A CR, LF or NUL crashed debug builds, and in release builds it
could end the header and start another on an HTTP/1 connection, for example when a module copied
request body bytes into a header. The filter now rejects an empty or invalid name and an invalid
value: ``set_header``, ``add_header`` and ``recreate_stream`` return false, as do ``get_header`` and
``get_header_values`` for an invalid name, and HTTP callouts and streams return
``CannotCreateRequest``. Local replies, response headers and trailers, and HTTP stream trailers
drop the invalid entry and send the rest. This change can be temporarily reverted by setting the
runtime guard ``envoy.reloadable_features.dynamic_modules_reject_invalid_headers`` to ``false``.
