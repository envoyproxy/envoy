.. _config_http_ai_filters_request_info:

Request info
============

The request info AI filter publishes the requested model, streaming preference,
output token cap, and message and tool counts as :ref:`envoy.data.ai.v3.RequestInfo
<envoy_v3_api_msg_data.ai.v3.RequestInfo>` typed dynamic metadata, under
``envoy.ai.request_info`` by default. It never modifies or rejects the request.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.http.ai_filters.request_info.v3.RequestInfo``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.ai_filters.request_info.v3.RequestInfo>`

Configuration
-------------

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling: {}
      filters:
      - name: envoy.http.ai_filters.request_info
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.request_info.v3.RequestInfo

Attributes are read according to the request's wire API: the one it declares,
or the one the :ref:`schema validation
<config_http_ai_filters_schema_validation>` filter resolved
when it runs first:

.. csv-table::
  :header: Attribute, OpenAI Chat Completions, OpenAI Responses, Anthropic Messages, Gemini
  :widths: 1, 1, 1, 1, 1

  ``model``, ``model``, ``model``, ``model``, model segment of the request path
  ``stream``, ``stream``, ``stream``, ``stream``, "``:generateContent`` (false) or ``:streamGenerateContent`` (true) operation in the request path"
  ``max_output_tokens``, "``max_completion_tokens``, else ``max_tokens``", ``max_output_tokens``, ``max_tokens``, "``generationConfig.maxOutputTokens`` (camelCase or snake_case)"
  ``message_count``, ``messages``, "``input`` (a string counts as one message)", ``messages``, ``contents``
  ``tool_count``, ``tools``, ``tools``, ``tools``, ``tools``

Without an API, only ``model`` and ``stream`` are read. Every value is
client-declared and optional. A value Envoy cannot use (wrong type, out of
range, or a string over 256 bytes) is ignored and counted by
``partial``.

The record is written before the held request headers are released, so later
decode filters see it from their first request-headers callback. An
:ref:`ext_proc <config_http_filters_ext_proc>` filter listed after this one
receives it through typed namespace forwarding:

.. code-block:: yaml

  metadata_options:
    forwarding_namespaces:
      typed:
      - envoy.ai.request_info

Only typed metadata is published. If the namespace already holds a record for
the stream, the new one is skipped and counted by ``duplicate``.

The model is also stored as the ``envoy.ai.model.request`` :ref:`filter state
object <well_known_filter_state>`, a string, so filters, the router and access
logs can read it without unpacking the record, e.g. with
``%FILTER_STATE(envoy.ai.model.request:PLAIN)%``. Nothing is stored when the
request names no usable model, and an object a filter ahead already set is
kept.

Token estimation
----------------

Configuring :ref:`token_estimation
<envoy_v3_api_field_extensions.http.ai_filters.request_info.v3.RequestInfo.token_estimation>`
adds ``estimated_input_tokens`` to the record, for consumers that must budget
before the provider reports what it charged: a rate limit on tokens per minute,
or a load balancer weighing queued work. It is ``ceil(tokens_per_byte *
request payload bytes)`` over the body Envoy buffered, so it reads no JSON and
is published whatever the route declares:

.. code-block:: yaml

  - name: envoy.http.ai_filters.request_info
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.ai_filters.request_info.v3.RequestInfo
      token_estimation:
        tokens_per_byte: 0.5

The ratio is a property of the payloads a deployment sees, and a size heuristic
is not a tokenizer: an estimate does not replace the :ref:`token usage
<envoy_v3_api_msg_data.ai.v3.TokenUsage>` the response reports. Rounding up
keeps a sub-token payload from estimating zero.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.request_info.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  published, Counter, The request info AI filter published an ``envoy.data.ai.v3.RequestInfo`` record.
  partial, Counter, A published request info record ignored at least one value Envoy could not use.
  duplicate, Counter, Request info publication skipped because another installation of the filter had already published the namespace for this stream.
