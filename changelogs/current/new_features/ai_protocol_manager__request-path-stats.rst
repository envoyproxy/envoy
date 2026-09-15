Added request-path statistics to the :ref:`AI Protocol Manager
<config_http_filters_ai_protocol_manager>` filter, whose counters previously
covered only the response path. ``request_parsed``, ``request_parse_error``,
``request_schema_invalid`` and ``request_passthrough`` make the decode path's
outcomes visible -- in particular telling a malformed payload apart from one
that parsed but failed its API's payload schema, since the two 400s mean
different things to act on. ``request_external_buffer_error`` and
``response_external_buffer_error`` count the 500 raised when the external
buffer fails irrecoverably on each direction.
