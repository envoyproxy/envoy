Added :ref:`external_buffer_config
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.external_buffer_config>`
to the :ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager>` (alpha), making the
store that offloaded payloads are written to a pluggable ``envoy.ai_protocol_manager.external_buffer``
extension. When unset, :ref:`envoy.ai_protocol_manager.external_buffer.in_memory
<envoy_v3_api_msg_extensions.ai_protocol_manager.external_buffer.in_memory.v3.InMemory>` is used; it
holds the payload in memory and does not bound its size.
