Added request-path statistics to the :ref:`AI Protocol Manager
<config_http_filters_ai_protocol_manager>` filter, whose counters previously
covered only the response path. ``request_payload_parsed``,
``request_parse_error``, ``request_schema_invalid`` and
``request_unparsed_passthrough`` make the decode path's outcomes visible --
in particular telling a malformed payload apart from one that parsed but
failed its API's payload schema, since the two 400s mean different things to
act on. ``external_buffer_error`` counts the 500 raised when the external
buffer fails irrecoverably, on either direction.
