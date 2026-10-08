Added request-path statistics to the :ref:`AI Protocol Manager
<config_http_filters_ai_protocol_manager>` filter, whose counters previously
covered only the response path. ``request_parsed``, ``request_parse_error``
and ``request_passthrough`` make the decode path's outcomes visible.
``request_external_buffer_error`` and ``response_external_buffer_error`` count
the 500 raised when the external buffer fails irrecoverably on each direction.
