Added :ref:`reserialize_body
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.reserialize_body>`
to the :ref:`AI Protocol Manager <config_http_filters_ai_protocol_manager>` filter. Set to
``DISABLE``, it forwards the received request body byte for byte instead of re-serializing the
parsed document, for AI filter chains that only read the request.
