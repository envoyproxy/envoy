.. _config_http_ai_filters_schema_validation:

Schema validation
=================

The schema validation AI filter validates the payload against the payload
schema of its wire API, for APIs with a defined schema (currently ``OPENAI_CHAT_COMPLETIONS``, ``ANTHROPIC_MESSAGES``
and ``GEMINI_GENERATE_CONTENT``). Validation checks required fields, data
types, enum values, and offload rules -- metadata fields such as ``model`` and
``role`` must stay inline, while large message content may reside in the
external buffer. A payload that fails is rejected with a 400, or forwarded
unchanged with :ref:`fail_open
<envoy_v3_api_field_extensions.http.ai_filters.schema_validation.v3.SchemaValidation.fail_open>`.

Configure it first. The API it resolves is the one the AI filters after it
read, and they only see a payload that passed.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.schema_validation.v3.SchemaValidation``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.schema_validation.v3.SchemaValidation>`

Configuration
-------------

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling: {}
      filters:
      - name: envoy.http.ai_filters.schema_validation
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.schema_validation.v3.SchemaValidation
      - name: envoy.http.ai_filters.request_info
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.request_info.v3.RequestInfo

The payload is validated against the API the request declares: the
``envoy.ai.llm_protocol.request`` filter state object, else the route's
:ref:`llm_protocol
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestPerRoute.llm_protocol>`.
A request that declares none falls back to :ref:`default_llm_protocol
<envoy_v3_api_field_extensions.http.ai_filters.schema_validation.v3.SchemaValidation.default_llm_protocol>`,
and by default the API is detected from the request: its path, matched as a
suffix against each provider's REST path (``/chat/completions``,
``/responses``, ``/v1/messages``, ``:generateContent``,
``:streamGenerateContent``); then the ``anthropic-version`` header; then the
payload's shape.

Chat Completions and Anthropic Messages share the ``messages[]`` request
shape, so only keys exclusive to one of them tell the two apart. A payload
carrying markers of both, or of neither -- including
``{"model": ..., "messages": [{"role": "user", "content": ...}]}``, which is
valid under either -- names no API, and is forwarded unvalidated.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.schema_validation.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  valid, Counter, The payload passed its API's schema.
  invalid, Counter, "The payload violated its API's schema: rejected with a 400, or forwarded with ``fail_open``."
  skipped, Counter, "No API was resolved, or the resolved one has no payload schema; the payload was forwarded unvalidated."
  llm_protocol_detected, Counter, The API was detected from the request.
