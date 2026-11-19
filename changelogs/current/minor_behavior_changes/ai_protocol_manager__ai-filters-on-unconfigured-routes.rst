The :ref:`AI filters
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.filters>`
now also run on requests parsed under :ref:`parse_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.parse_unconfigured_routes>`,
so a catch-all route such as a dynamic forward proxy's can rely on schema validation to detect the
API. A payload that parses is re-serialized like a declared endpoint's unless ``reserialize_body``
is ``DISABLE``; one that fails to parse is still forwarded unchanged. Responses on such a route are
not passed to the AI filters.
