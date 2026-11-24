AI filters of the :ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager>` (alpha)
can now answer a declared endpoint's request that has no body or is not JSON, send a local reply
with a body and headers of their own, clear the route cache, and be configured per route with
:ref:`typed_per_filter_config
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.typed_per_filter_config>`.
