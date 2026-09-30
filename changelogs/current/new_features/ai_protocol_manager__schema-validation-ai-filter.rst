Added the :ref:`envoy.http.ai_filters.schema_validation
<envoy_v3_api_msg_extensions.http.ai_filters.schema_validation.v3.SchemaValidation>` AI filter to the
:ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager>`, which now runs request
payload schema validation instead of the filter core. It validates against the request's declared
wire API, else a configured default, else one detected from the request, and rejects a violating
payload with a 400 unless ``fail_open`` is set. A filter ahead of the AI Protocol Manager can name
the request's wire API with the ``envoy.ai.llm_protocol.request`` filter state object, which takes
precedence over the route. The core's ``request_schema_invalid`` counter is replaced by the AI
filter's own statistics.
