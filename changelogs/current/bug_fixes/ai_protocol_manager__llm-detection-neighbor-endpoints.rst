Fixed the ``schema_validation`` AI filter detecting requests to a provider's other endpoints as a
chat API, which rejected ``/v1/messages/count_tokens`` and ``/v1/messages/batches`` requests with a
``400`` against the Anthropic Messages schema and treated embeddings, moderations and speech
requests as OpenAI Responses. A path under ``/v1/messages``, ``/chat/completions`` or
``/responses`` now detects nothing, and neither does a Gemini payload sent to a method other than
``:generateContent`` or ``:streamGenerateContent``. A payload is detected as Anthropic Messages
only when it carries ``max_tokens``, the ``anthropic-version`` header now only tells such a payload
apart from a Chat Completions one, and the Responses API is detected by its path only.
