.. _config_http_filters_ai_protocol_manager:

AI Protocol Manager
===================

The AI Protocol Manager filter (alpha) buffers the request payload off the
connection manager's hot path so that routing and admission decisions can be
made on the fully received body.

As the request body arrives, the filter offloads it into an external buffer
rather than pinning it in the connection manager's in-memory buffers. Once the
stream ends, it streams the buffered bytes back into the filter chain so that
the subsequent filters observe the request unchanged. The offload/replay
round-trip is flow-controlled in both directions: ingest honors the buffer
limit, and replay is paced against filter-chain back-pressure, so the resident
footprint stays bounded regardless of payload size.

While a body is being offloaded, the request headers are held at this filter and
released to the subsequent filters only once replay begins, so they never act on
the headers before the payload they depend on is available.

.. note::

  Only the request (decode) path is wired today, and the body is offloaded to an
  in-memory store. The filter performs a straight offload-then-replay; streaming
  payload parsing and admission control will be layered on top of this plumbing.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager>`

Configuration
-------------

The filter takes no configuration. Example:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
