The :ref:`AI filters
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.filters>`
now also run on requests parsed under :ref:`parse_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.parse_unconfigured_routes>`,
so a catch-all route such as a dynamic forward proxy's can rely on schema validation to detect the
API. A payload that fails to parse on such a route is still forwarded unchanged.
