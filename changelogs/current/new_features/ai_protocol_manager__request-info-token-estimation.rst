Added :ref:`token_estimation
<envoy_v3_api_field_extensions.http.ai_filters.request_info.v3.RequestInfo.token_estimation>` to the
``envoy.http.ai_filters.request_info`` AI filter (alpha). When configured, the published
:ref:`envoy.data.ai.v3.RequestInfo <envoy_v3_api_msg_data.ai.v3.RequestInfo>` record carries
``estimated_input_tokens``, computed as ``ceil(tokens_per_byte * request payload bytes)``, for
consumers that must budget before the provider reports usage.
