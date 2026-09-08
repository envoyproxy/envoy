Added an AI filter chain to the :ref:`AI Protocol Manager filter
<config_http_filters_ai_protocol_manager>` (alpha, work-in-progress API): the new
:ref:`filters
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.filters>`
field runs ``envoy.filters.ai`` extensions in order over a declared AI endpoint's parsed
payload before it is replayed. Added the first such filter, :ref:`envoy.filters.ai.request_info
<envoy_v3_api_msg_extensions.filters.ai.request_info.v3.RequestInfo>`, which publishes the model,
streaming preference, output token cap, and message and tool counts of OpenAI, Anthropic and
Gemini requests as :ref:`envoy.data.ai.v3.RequestInfo <envoy_v3_api_msg_data.ai.v3.RequestInfo>`
typed dynamic metadata while the request headers are still held, so later filters such as
ext_proc read it from their first request-headers callback.
