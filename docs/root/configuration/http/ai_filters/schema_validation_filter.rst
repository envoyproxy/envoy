.. _config_http_ai_filters_schema_validation:

Schema validation
=================

The schema validation AI filter validates the payload against the payload
schema of its wire API, for APIs with a defined schema (currently
``OPENAI_CHAT_COMPLETIONS``, ``ANTHROPIC_MESSAGES`` and
``GEMINI_GENERATE_CONTENT``). Validation checks required fields, data
types, enum values, and offload rules -- metadata fields such as ``model`` and
``role`` must stay inline, while large message content may reside in the
external buffer. A payload that fails is rejected with a ``400``, or forwarded
unchanged with :ref:`fail_open
<envoy_v3_api_field_extensions.http.ai_filters.schema_validation.v3.SchemaValidation.fail_open>`.

Configure it first. The API it resolves is the one the AI filters after it
read, and they only see a payload that passed.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.schema_validation.v3.SchemaValidation``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.schema_validation.v3.SchemaValidation>`

Configuration
-------------

.. literalinclude:: _include/model-routing.yaml
    :language: yaml
    :lines: 36-46
    :lineno-start: 36
    :emphasize-lines: 6-8
    :linenos:
    :caption: :download:`model-routing.yaml <_include/model-routing.yaml>`

The payload is validated against the API the request declares: the
``envoy.ai.llm_protocol.request`` filter state object, else the route's
:ref:`llm_protocol
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestPerRoute.llm_protocol>`.
A request that declares none falls back to :ref:`default_llm_protocol
<envoy_v3_api_field_extensions.http.ai_filters.schema_validation.v3.SchemaValidation.default_llm_protocol>`,
and when that is unset too, the API is detected from the request.

.. _config_http_ai_filters_schema_validation_detection:

API detection
-------------

The path decides, and the payload only when the path names no API. The first
row that matches wins:

.. csv-table::
  :header: The request, Detected API
  :widths: 3, 1

  "Path ends with ``/chat/completions``", ``OPENAI_CHAT_COMPLETIONS``
  "Path ends with ``/responses``", ``OPENAI_RESPONSES``
  "Path ends with ``/v1/messages``", ``ANTHROPIC_MESSAGES``
  "Path ends with ``:generateContent`` or ``:streamGenerateContent``", ``GEMINI_GENERATE_CONTENT``
  "Path goes on after one of the first three, as in ``/v1/messages/count_tokens``", None
  "Payload has Gemini markers, but the path ends with another method, as in ``:countTokens``", None
  "Payload has Gemini markers", ``GEMINI_GENERATE_CONTENT``
  "Payload has ``messages`` and only Chat Completions markers", ``OPENAI_CHAT_COMPLETIONS``
  "Payload has ``messages``, ``max_tokens`` and only Anthropic markers", ``ANTHROPIC_MESSAGES``
  Anything else, None

The path is matched without its query and as a suffix, so a gateway prefix such
as ``/anthropic/v1/messages`` does not hide it.

Markers are what only one API has:

* Gemini: ``contents``, ``systemInstruction`` or ``generationConfig``, the last
  two in either camel or snake case.
* Chat Completions: ``max_completion_tokens``, ``response_format``,
  ``frequency_penalty``, ``presence_penalty``, ``stream_options``,
  ``logit_bias``, a tool with ``function``, or a message whose role is
  ``system``, ``developer`` or ``tool``.
* Anthropic: ``system``, ``stop_sequences``, ``top_k``, ``thinking``, a tool
  with ``input_schema``, or an ``anthropic-version`` request header.

A wrong guess rejects a valid request, so detection gives up when unsure:

* Markers of both APIs, or of neither, detect nothing. That includes
  ``{"model": ..., "messages": [{"role": "user", "content": ...}]}``, which is
  valid under both.
* Without ``messages`` in the payload, an ``anthropic-version`` header detects
  nothing, because Anthropic clients send it to every endpoint.
* Anthropic Messages needs ``max_tokens``, which that API requires and a
  token-counting request does not carry.
* The Responses API is detected by its path only, because
  ``{"model": ..., "input": ...}`` is also an embeddings request.

A request whose API is not detected is forwarded unvalidated and counted as
``skipped``. Name its API on the route or with ``default_llm_protocol`` to
validate it.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.schema_validation.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  valid, Counter, The payload passed its API's schema.
  invalid, Counter, "The payload violated its API's schema: rejected with a ``400``, or forwarded with ``fail_open``."
  skipped, Counter, "No API was resolved, or the resolved one has no payload schema; the payload was forwarded unvalidated."
  llm_protocol_detected, Counter, The API was detected from the request.
