The :ref:`AI filters
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.filters>`
now also run on requests parsed under :ref:`parse_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.parse_unconfigured_routes>`,
so a catch-all route such as a dynamic forward proxy's can rely on schema validation to detect the
API. A payload that parses is re-serialized like a declared endpoint's unless ``reserialize_body``
is ``DISABLE``; one that fails to parse is still forwarded unchanged. An API an AI filter detects is
published as ``envoy.ai.llm_protocol.request`` filter state, and the response then runs the AI
filters as a declared endpoint's does. A response no longer runs a chain in which no AI filter
handles responses.
