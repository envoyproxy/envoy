``ai_protocol_manager``: ``schema_validation`` and ``request_info`` now read the request path the
same way the transcoder does. A request may arrive in a provider's layout of its API rather than
the API's own, such as Claude on Vertex AI, which names the model in the path instead of the body.
``schema_validation`` counts a model the path names as present, and ``request_info`` takes the
model and the streaming mode from the path when it names them. The Gemini API binding also
matches ``/{version}/models/{model}:...`` under any API version, and Gemini on Vertex AI with or
without a project and location. ``request_info`` used to read any path ending in
``/models/{model}:generateContent`` or ``:streamGenerateContent``; a path under another prefix,
such as ``/gemini/v1beta/models/{model}:generateContent``, now needs a
:ref:`request binding <config_http_ai_filters_transcoder_instructions>`
on the route to be read.
