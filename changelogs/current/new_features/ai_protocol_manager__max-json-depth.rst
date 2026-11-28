Added ``max_json_depth`` to the :ref:`request
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestParsingLimits.max_json_depth>`
and :ref:`response
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.ResponseParsingLimits.max_json_depth>`
parsing limits of the :ref:`AI Protocol Manager filter <config_http_filters_ai_protocol_manager>`
(alpha), the deepest JSON nesting a payload may have. Both default to 64, where the limit was fixed
at 16 before, which rejected tool and response schemas only a few objects deep. A request over the
limit is now told apart from malformed JSON: it is counted by ``request_json_depth_exceeded``, and a
declared AI endpoint rejects it with the response code details
``ai_protocol_manager_json_depth_exceeded``.
