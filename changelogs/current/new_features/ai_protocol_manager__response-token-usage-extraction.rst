Added response-side LLM token-usage extraction to the
:ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager>`
(alpha, work-in-progress API): streaming SSE and JSON responses in the OpenAI,
Anthropic, and Gemini dialects are observed without stopping filter-chain
iteration or mutating the response (extraction runs synchronously on the
encode callbacks against a bounded side copy), and normalized usage is
published as typed dynamic metadata (default namespace
``envoy.ai.token_usage``): the authoritative record is
:ref:`envoy.data.ai.v3.TokenUsage <envoy_v3_api_msg_data.ai.v3.TokenUsage>`,
consumable via ext_proc typed metadata forwarding or any filter reading
typed dynamic metadata. Inspection is scoped to
routes carrying an
:ref:`AiProtocolManagerPerRoute
<envoy_v3_api_msg_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute>`
configuration, with :ref:`include_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction.include_unconfigured_routes>`
widening it to every route. Extraction behaves identically in the downstream
and upstream (cluster, e.g. dynamic-forward-proxy egress) installations of
the filter, and leaving :ref:`request_handling
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.request_handling>`
unset yields a response-only installation whose request path is a pure
passthrough.
