Fixed the ``schema_validation`` AI filter detecting requests to a provider's other endpoints as a
chat API: ``/v1/messages/count_tokens`` requests were rejected with a ``400``, and embeddings
requests were treated as OpenAI Responses calls. :ref:`API detection
<config_http_ai_filters_schema_validation_detection>` describes the rules.
