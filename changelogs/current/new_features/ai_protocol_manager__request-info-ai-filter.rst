Added an AI filter chain to the :ref:`AI Protocol Manager filter
<config_http_filters_ai_protocol_manager>` (alpha): the :ref:`filters
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.filters>`
field runs ``envoy.http.ai_filters`` extensions over a declared AI endpoint's parsed payload before
it is replayed. The first one, :ref:`envoy.http.ai_filters.request_info
<envoy_v3_api_msg_extensions.http.ai_filters.request_info.v3.RequestInfo>`, publishes request
attributes as :ref:`envoy.data.ai.v3.RequestInfo <envoy_v3_api_msg_data.ai.v3.RequestInfo>`
typed dynamic metadata before the request headers continue.
