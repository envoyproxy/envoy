Fixed the ``ai_protocol_manager`` ``schema_validation`` and ``request_info`` filters not reading
the binding a request arrived in. A Claude on Vertex AI request names the model in the path and
not in the body, so ``schema_validation`` rejected it as missing ``model`` and ``request_info``
published no model and the wrong streaming mode; only the transcoder read the path. Both filters
now read it through the same built-in and route bindings as the transcoder, without editing the
body. The Gemini API binding also matches ``/{version}/models/{model}:...`` under any API version,
as ``request_info`` already did.
