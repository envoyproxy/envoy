.. _config_http_filters_ai_protocol_manager:

AI Protocol Manager
===================

The AI Protocol Manager filter (alpha) buffers the request payload off the
connection manager's hot path so that routing and admission decisions can be
made on the fully received body.

It does so only for requests it has a reason to inspect. A request on a route
that is not a declared AI endpoint -- and, unless :ref:`best_effort_parsing
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.best_effort_parsing>`
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

  Only the request (decode) path is wired today, and the body is offloaded to an
  in-memory store. Request schema validation is supported for declared schemas
  (such as OpenAI Chat Completions). Response path handling and schema transcoding
  are not implemented yet.

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
A route that carries one names the :ref:`schema
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.schema>`
its payload follows, and its payload is parsed and validated strictly: a malformed or invalid body is
rejected with a 400. This is normally attached to a route matching the
provider's REST path, such as ``/chat/completions``:

.. code-block:: yaml

  routes:
  - match:
      path: "/chat/completions"
    route:
      cluster: openai
    typed_per_filter_config:
      envoy.filters.http.ai_protocol_manager:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute
        schema: OPENAI_CHAT_COMPLETIONS

Such a route is a pass-through endpoint: the payload is forwarded upstream in
its own schema. Setting :ref:`normalize
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.normalize>`
additionally transcodes it into the canonical schema, which is what lets one set
of filters operate on payloads from different providers.

The filter-level configuration decides what happens on every other route. By
default those requests are passed through untouched -- not parsed, and not
offloaded; setting
:ref:`best_effort_parsing
<envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager.best_effort_parsing>`
offloads and parses them too, but never fails a request over it -- a payload that
does not parse is forwarded unchanged.

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      best_effort_parsing: true

