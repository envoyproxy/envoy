ai_protocol_manager: added :ref:`usage_signal
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction.usage_signal>`
to :ref:`TokenUsageExtraction
<envoy_v3_api_msg_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction>`.
Setting ``usage_signal`` to ``SYNTHESIZE_TRAILERS`` adds empty response trailers at a
clean end of stream when the response carries none of its own, allowing trailer-driven
consumers such as ``ext_proc`` with ``response_trailer_mode: SEND`` to receive the
published token usage metadata without streaming the response body.
