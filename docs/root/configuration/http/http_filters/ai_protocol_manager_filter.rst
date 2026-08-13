.. _config_http_filters_ai_protocol_manager:

AI Protocol Manager
===================

The AI Protocol Manager filter (alpha) manages AI API traffic on both
directions of a stream:

* On the **request (decode) path** it buffers a declared AI endpoint's payload
  off the connection manager's hot path — parsing it as it arrives — so that
  routing and admission decisions can be made on the fully received body.
* On the **response (encode) path** it can extract normalized LLM **token
  usage** from provider responses — OpenAI (Chat Completions and Responses
  API), Anthropic (Messages API), and Gemini (``generateContent`` /
  ``streamGenerateContent``) — and publish it as dynamic metadata for
  consumption by later filters (e.g. :ref:`ext_proc
  <config_http_filters_ext_proc>` metadata forwarding), access loggers, and
  CEL expressions.

Request and response processing are enabled independently, by the presence of
:ref:`request_handling
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.request_handling>`
and :ref:`response_handling
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.response_handling>`.

The filter acts only on requests it has a reason to inspect. A request on a
route that is not a declared AI endpoint -- and, unless
:ref:`parse_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.parse_unconfigured_routes>`
is set, every request -- passes straight through: its headers are not held, its
body is not offloaded, and no external buffer is created for it. A filter chain
carrying this filter therefore costs ordinary pass-through for the traffic it
does not serve.

For a request it does inspect, as the body arrives the filter offloads it into an external buffer
rather than pinning it in the connection manager's in-memory buffers. Once the
stream ends, it streams the buffered bytes back into the filter chain so that
the subsequent filters observe the request unchanged. The offload/replay
round-trip is flow-controlled in both directions: ingest honors the buffer
limit, and replay is paced against filter-chain back-pressure, so the resident
footprint stays bounded regardless of payload size.

While such a body is being offloaded, the request headers are held at this filter
and released to the subsequent filters only once replay begins, so they never act
on the headers before the payload they depend on is available.

On a route declared to be an AI endpoint, the body is parsed as it is offloaded,
so that a payload which is not well-formed JSON is rejected here rather than
forwarded for the upstream to interpret differently. Parsing is incremental and shares the offload's byte
stream, so an invalid payload fails as soon as the offending byte arrives rather
than after the whole upload. Oversized string values are left in the external
buffer and referenced by offset, so a large prompt does not reappear in
per-stream memory.

Upon stream completion, the parsed document is validated against the route's declared
:ref:`schema <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.schema>`
(such as ``OPENAI_CHAT_COMPLETIONS``). Validation checks required fields, data types, enum values,
and offload rules -- ensuring metadata fields (like ``model`` and ``role``) remain inline in the DOM
while permitting large message content to reside in external buffers. Any schema validation failure
triggers an immediate HTTP 400 response.

.. note::

  On the request path the body is offloaded to an in-memory store. Request
  schema validation is supported for declared APIs with a defined schema
  (currently OpenAI Chat Completions); schema transcoding is not implemented
  yet.

The filter is a dual filter: besides the downstream HTTP filter chain shown
below, it can also be placed in a cluster's upstream HTTP filter chain via
:ref:`http_filters <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.http_filters>`,
where the offload/replay round-trip runs after load balancing and host
selection (and therefore once per retry or hedged attempt).

.. note::

  Two caveats apply to the upstream placement, and only to routes the filter
  inspects. The filter holds the request headers until the payload has been
  fully offloaded, and upstream filter
  chains have no per-upgrade-type chain selection (the
  :ref:`upgrade_configs <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.upgrade_configs>`
  escape hatch is downstream-only), so the filter must not front upgrade or
  CONNECT routes, or other requests whose body does not end promptly: such
  streams would stall until the request times out. Additionally, local replies
  raised from an upstream filter chain (such as this filter's external-buffer
  error reply) are delivered directly to the downstream client without
  consulting the router's retry or hedging logic.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager>`

Configuration
-------------

Which routes are AI endpoints is declared per route, with
:ref:`AiProtocolManagerPerRoute
<envoy_v3_api_msg_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute>`.
A route carrying a :ref:`request
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.request>`
declaration names the :ref:`wire API
<envoy_v3_api_enum_type.ai.v3.ApiProtocol>` its request payload follows, and
(when ``request_handling`` is enabled) its payload is parsed and validated
strictly: a malformed body — or one violating the declared API's payload
schema, for APIs with a defined schema — is rejected with a 400. This is
normally attached to a route matching the provider's REST path, such as
``/chat/completions``:

.. code-block:: yaml

  routes:
  - match:
      path: "/chat/completions"
    route:
      cluster: openai
    typed_per_filter_config:
      envoy.filters.http.ai_protocol_manager:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute
        request:
          api_protocol: OPENAI_CHAT_COMPLETIONS

The request and response wire APIs are declared separately (an optional
:ref:`response
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.response>`
declaration covers gateways whose response API differs from the request API,
e.g. under protocol translation); when the response API is undeclared it
falls back to the request API.

The filter-level configuration decides what happens on every other route. By
default those requests are passed through untouched -- not parsed, and not
offloaded; setting
:ref:`parse_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.parse_unconfigured_routes>`
offloads and parses them too, but never fails a request over it -- a payload that
does not parse is forwarded unchanged.

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling:
        parse_unconfigured_routes: true

Response token-usage extraction
-------------------------------

When :ref:`response_handling.token_usage
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.ResponseHandling.token_usage>`
is configured, 2xx responses with a ``text/event-stream`` or
``application/json`` content type are observed as they stream through the
filter. Inspection is scoped to routes carrying a per-route configuration;
setting :ref:`include_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction.include_unconfigured_routes>`
widens it to every route, for response-only installations whose routes cannot
be declared (for example a dynamic-forward-proxy cluster). The filter **never stops iteration or modifies the original
response**, and no extraction failure can affect it. Extraction works on a
separately bounded side copy — hard-capped at :ref:`max_sse_event_size
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtractionLimits.max_sse_event_size>`
(SSE) or :ref:`max_json_body_size
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtractionLimits.max_json_body_size>`
(JSON) per stream, regardless of how large individual data frames are, and
charged to the stream's buffer memory account when :ref:`account tracking
<envoy_v3_api_field_config.overload.v3.BufferFactoryConfig.minimum_account_to_track_power_of_two>`
is enabled — and the extraction work runs on the response's filter-chain
callbacks. Parsing uses the filter's shared streaming JSON machinery
(``JsonWithExtBufParser``): a JSON body streams incrementally into the parser
with no retained side copy, and oversized string values are never
materialized in the document. A complete SSE event
arriving within one data frame — the dominant shape of real streams — is
processed in place and retains nothing; only an event split across frames is
buffered (consolidating a split event can transiently hold a second
account-charged copy, bounded by the cap). Only the retained copy is
account-charged: the transient allocations of parsing a completed event or
body (the assembled event data string and the JSON document tree) are
short-lived, bounded by the same caps, and not charged. Named events that
cannot carry usage (keepalives, content deltas, non-terminal OpenAI
Responses lifecycle events) are dropped on their ``event:`` line alone,
before any payload is assembled or parsed, and per-stream parse work is
bounded by :ref:`max_parsed_sse_events
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtractionLimits.max_parsed_sse_events>`.

Responses with a non-identity ``content-encoding`` are skipped (counted by
``unsupported_content_encoding``). To extract usage from compressed provider
responses:

* **Downstream installation**: place the :ref:`decompressor filter
  <config_http_filters_decompressor>` so that it runs before this filter on
  the encode path (list it *after* this filter in ``http_filters``, since
  encoder filters run in reverse order).
* **Upstream installation**: Envoy's decompressor is a downstream-only filter
  today, so it cannot run before a cluster-installed AI Protocol Manager. The
  practical approach is to prevent compression on the provider connection —
  remove or pin the request's ``accept-encoding`` header on the cluster's
  filter chain (e.g. with the :ref:`header mutation filter
  <config_http_filters_header_mutation>`, which supports upstream
  installation) so the provider responds with identity encoding.

Both streaming shapes are handled:

* SSE streams (OpenAI chunks and lifecycle events, Anthropic named events,
  Gemini ``?alt=sse``), reassembled across arbitrary frame boundaries.
* JSON bodies, including Gemini's default (non-SSE) streaming whose complete
  body is a root-level JSON array of chunks.

The response's :ref:`wire API <envoy_v3_api_enum_type.ai.v3.ApiProtocol>` is
resolved in precedence order: the route's declared response API, the route's
declared request API, then :ref:`default_api_protocol
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction.default_api_protocol>`;
when none is declared it is auto-detected from the response shape (only
strong, dialect-unique markers lock detection).
Cumulative streaming counters (Anthropic ``message_delta``, Gemini snapshots)
are merged with last-value-wins semantics per native field, and the canonical
values are computed once, after the last event.

All emitted counts follow one canonical, **inclusive** contract regardless of
dialect: ``input_tokens`` covers all input consumed (uncached input, cached
reads, cache writes, and tool-use prompt tokens), ``output_tokens`` covers all
generated output including reasoning/thought tokens, and ``total_tokens`` is
computed as their sum and emitted only when both components are known, so the
emitted triple is always internally consistent. The total the provider itself
reported is always preserved separately as ``provider_total_tokens``, whether
or not it agrees with the canonical sum (a disagreement — an internally
inconsistent response, or a usage bucket unknown to the extractor — is
counted by ``token_usage_total_mismatch``). The breakdown fields
(``input_token_details``, ``output_token_details``) are parts of the
canonical values. The extractor
normalizes each dialect's native shape onto this contract — for example
Anthropic's disjoint cache buckets are summed into the canonical input, and
Gemini's thoughts are summed into the canonical output.

.. note::

  The canonical fields carry consistent *inclusion semantics* across dialects
  — which native buckets are counted — not equivalent units of work or cost.
  Tokenizer behavior, model vocabulary, cache pricing, and provider pricing
  all differ; cost accounting additionally needs provider/model identity and
  a pricing model.

At end of stream the result is published under the metadata namespace
(default ``envoy.ai.token_usage``) in **both metadata forms**:

* **Typed dynamic metadata** — the authoritative record, an
  :ref:`envoy.data.ai.v3.TokenUsage <envoy_v3_api_msg_data.ai.v3.TokenUsage>`
  message with full ``uint64`` precision and enum-typed status fields. Typed
  consumers (e.g. ext_proc via ``forwarding_namespaces.typed``) should prefer
  it.
* **An untyped Struct projection** — identical field names and values,
  projected from the typed record, for consumers that read untyped metadata
  only (``%DYNAMIC_METADATA%``, CEL). Its counts are double-backed and
  therefore bounded to exactly-representable integers:

.. code-block:: yaml

  api_protocol: "ANTHROPIC_MESSAGES"  # the wire API, as an enum value name
  model: "claude-opus-5"              # response-reported model, when present
  input_tokens: 1200
  output_tokens: 350
  total_tokens: 1550                  # input + output; only when both are known
  provider_total_tokens: 1550         # the provider's own total, always preserved
  input_token_details:                # when reported; parts of input_tokens
    cached_tokens: 800
    cache_creation_tokens: 0
    tool_use_tokens: 0
  output_token_details:               # when reported; parts of output_tokens
    reasoning_tokens: 120
  extraction_status: "COMPLETE"       # COMPLETE | PARTIAL | FAILED

``api_protocol`` names the wire API the response spoke, deliberately not a
provider identity: shape detection cannot distinguish an OpenAI-compatible
backend (vLLM, other gateways, Gemini's compatibility endpoint) from OpenAI
itself. When the actual provider identity is needed, derive it from
configuration or routing (for example cluster metadata), not from this field.

.. warning::

  Every count (and the model name) is **provider-reported and unverified**:
  Envoy does not count tokens itself. The upstream controls these values and
  can report zero, inconsistent, or inflated numbers. Treat the metadata as an
  observability signal for trusted providers; for billing or quota enforcement
  against untrusted destinations, independent verification is required.

Metadata is only published at a clean end of the HTTP response (a reset or
abandoned stream publishes nothing). When extraction failed outright — the
only usage-bearing event exceeded a cap, or every usage document was
malformed, unparseable, or truncated — a **status-only** record is published
(``api_protocol``, ``model`` when captured, and ``extraction_status:
FAILED``, with no counts), so per-stream consumers can distinguish
"extraction failed" from "the provider supplied no usage", which publishes
nothing and counts ``token_usage_missing``. ``extraction_status`` reports
extraction quality: ``COMPLETE`` means every observed usage document was
extracted; ``PARTIAL`` means usable counts were published but extraction lost
input on this stream — an event over :ref:`max_sse_event_size
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtractionLimits.max_sse_event_size>`,
an unparseable document, a known usage field carrying a malformed value
(wrong type, negative, fractional, null, or out of range), SSE input
truncated before its terminating blank line, or a canonical sum exceeding the
exactly-representable bound — so the counts may be stale (for example an
earlier cumulative snapshot) or incomplete. No status implies the model
response succeeded (an OpenAI ``response.failed`` event carrying usage is
still published). Usage can be legitimately absent (for example an OpenAI
stream without ``stream_options.include_usage``); this is counted by the
``token_usage_missing`` statistic and no metadata is written.

The published values are **selected-response usage**, not the total provider
cost of servicing the downstream request: with retries or hedging only the
attempt whose response the router selects publishes (a losing attempt can
still consume billable tokens upstream), and any 2xx body containing usage
fields — including one served by a cache or generated locally between this
filter and the client-facing edge — is extracted even though no provider call
may have occurred for it. Aggregating true per-request provider cost requires
per-attempt accounting outside this filter.

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      response_handling:
        token_usage: {}

Filter ordering
~~~~~~~~~~~~~~~

Metadata is written when the final response data frame (or the response
trailers) passes this filter. A consumer that reads metadata on the encode
path — such as an ext_proc filter using
:ref:`metadata_options.forwarding_namespaces
<envoy_v3_api_field_extensions.filters.http.ext_proc.v3.ExternalProcessor.metadata_options>`
— must run *after* the AI Protocol Manager on the encode path to observe the
usage on that same end-of-stream frame:

* Downstream installation: encoder filters run in the reverse of the
  ``http_filters`` order, so list the consumer *before* this filter.
* Upstream installation: all upstream encoder filters run before every
  downstream encoder filter, so any downstream consumer observes the metadata
  without ordering constraints.

An ext_proc consumer additionally needs a :ref:`processing mode
<envoy_v3_api_msg_extensions.filters.http.ext_proc.v3.ProcessingMode>` under
which the external processor actually receives a message at or after end of
stream — the metadata does not exist yet at response-header time, and by
default ext_proc sends no response-body messages (``response_body_mode:
NONE``) and skips response trailers. Configure ``response_body_mode: STREAMED``
(the terminal, end-of-stream body message carries the metadata context), or
``response_trailer_mode: SEND`` for trailer-ended responses; forwarding the
namespace alone is not sufficient:

.. code-block:: yaml

  processing_mode:
    response_body_mode: STREAMED   # or response_trailer_mode: SEND
  metadata_options:
    forwarding_namespaces:
      untyped:
      - envoy.ai.token_usage

Consumers that only read metadata at end of stream (access loggers, CEL in
access-log filters) are unaffected by ordering.

Upstream (cluster) installation
-------------------------------

The filter is also registered as an upstream HTTP filter for deployments where
handling must live on the cluster — for example a dynamic-forward-proxy egress
cluster whose destination is only known per request. The upstream installation
is the full filter: the request-path offload/replay runs there too, once per
retry or hedged attempt, with the caveats described under the request payload
offload section above. Response token-usage extraction behaves identically in
either chain.

Dynamic metadata written from the upstream installation lands on the
downstream stream's metadata and is visible to downstream filters, access
loggers, and CEL exactly as in a downstream installation.

.. note::

  Enable response handling in a **single** placement per metadata namespace.
  With both placements enabled at once, the first publication owns the
  namespace for the stream -- the upstream instance, since upstream encoder
  filters run before downstream encoder filters -- and later instances skip
  publishing (counted by ``token_usage_duplicate``) rather than merging two
  observations into one hybrid record. Give each placement its own
  :ref:`metadata_namespace
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction.metadata_namespace>`
  to capture both.

Response-only installation: a deployment that installs the filter purely to
observe responses — for example token-usage extraction on a
dynamic-forward-proxy egress cluster — leaves ``request_handling`` unset (the
decode path is then a pure passthrough: no header hold, no offload, no
per-attempt replay) and, since such routes declare no per-route
configuration, sets :ref:`include_unconfigured_routes
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtraction.include_unconfigured_routes>`:

.. code-block:: yaml

  response_handling:
    token_usage:
      include_unconfigured_routes: true

Upstream retries and hedging: each upstream attempt runs its own filter
instance, but only the attempt whose response the router selects streams to a
clean end of stream through its encoder chain — and non-2xx attempts never
engage extraction — so only the winning attempt publishes metadata.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  token_usage_found, Counter, A response yielded token usage and metadata was written (includes ``PARTIAL`` records).
  token_usage_partial, Counter, A published record was flagged ``extraction_status: PARTIAL``.
  token_usage_failed, Counter, A status-only record was published (``extraction_status: FAILED``; no counts recovered).
  token_usage_missing, Counter, A handled response ended with no usage to extract.
  token_usage_total_mismatch, Counter, The provider-reported total disagreed with the canonical input + output sum.
  token_usage_duplicate, Counter, Publication skipped because another installation of the filter had already published the namespace for this stream.
  malformed_usage_field, Counter, "A document carried a known usage field with an unusable value (wrong type, negative, fractional, or out of range); the response is flagged partial."
  sse_incomplete_event, Counter, Non-empty SSE input could not form a complete event by end of stream and was discarded; the response is flagged partial.
  sse_event_budget_exhausted, Counter, The stream hit :ref:`max_parsed_sse_events <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.TokenUsageExtractionLimits.max_parsed_sse_events>`; extraction went inert and the response is flagged partial.
  response_parse_error, Counter, A JSON body or SSE event payload failed to parse (per occurrence; the stream is unaffected).
  response_body_too_large, Counter, A JSON response exceeded ``max_json_body_size``; extraction skipped.
  sse_event_too_large, Counter, Pending or complete SSE event data exceeded ``max_sse_event_size``; that entire event was skipped.
  unsupported_content_encoding, Counter, The response carried a non-identity ``content-encoding``; extraction skipped.
