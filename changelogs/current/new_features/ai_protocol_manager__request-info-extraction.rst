Added :ref:`extraction
<envoy_v3_api_field_extensions.http.ai_filters.request_info.v3.RequestInfo.extraction>` to the
:ref:`request info AI filter <config_http_filters_ai_protocol_manager_request_info_extraction>`:
configurable JSON paths whose values, arrays and objects included, are copied out of the parsed
request body into untyped dynamic metadata under configurable per-stream byte, depth and string
limits, so consumers that never see the body (an external processor running on request headers, a
CEL matcher, a route's ``metadata_match``) can act on a policy the client sent alongside the prompt.
