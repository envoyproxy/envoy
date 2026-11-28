.. _config_http_ai_filters_mcp_json_rest_bridge:
.. _extension_envoy.http.ai_filters.mcp_json_rest_bridge:

MCP JSON REST bridge
====================

The MCP JSON REST bridge AI filter lets a JSON REST backend serve Model Context
Protocol clients: it answers the MCP handshake itself, maps a ``tools/call``
request onto the REST request of the tool, and returns the backend's response as
the JSON-RPC result.

It runs the bridge of the ``envoy.filters.http.mcp_json_rest_bridge`` HTTP filter
and takes its configuration, so the two behave alike and a feature of one is a
feature of the other. Declare the routes that carry MCP requests as AI endpoints;
they need no ``llm_protocol``.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge>`

Configuration
-------------

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling: {}
      filters:
      - name: envoy.http.ai_filters.mcp_json_rest_bridge
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge
          tool_config:
            tools:
            - name: get_api_key
              http_rule:
                get: "/v1/{name=apiKeys/*}"

  routes:
  - match:
      path: /mcp
    route:
      cluster: rest_backend
    typed_per_filter_config:
      envoy.filters.http.ai_protocol_manager:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute
        request: {}

Differences from the HTTP filter
--------------------------------

What differs from the HTTP filter is where the bridge is hosted:

* A route's :ref:`McpJsonRestBridgePerRoute
  <envoy_v3_api_msg_extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridgePerRoute>`
  goes in the manager's :ref:`typed_per_filter_config
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.typed_per_filter_config>`
  under the filter's name.
* Dynamic metadata is stored under the filter's name in ``filters``.
* The request is re-serialized, so :ref:`reserialize_body
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.reserialize_body>`
  has to stay ``ALWAYS``.
* The bridge reads argument values, so it raises the manager's
  :ref:`inline_string_threshold_bytes
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestParsingLimits.inline_string_threshold_bytes>`
  to its own :ref:`max_request_body_size
  <envoy_v3_api_field_extensions.filters.http.mcp_json_rest_bridge.v3.McpJsonRestBridge.max_request_body_size>`,
  at most 1MiB, on every route of the filter. A request with a longer string
  value is rejected as too large.
* ``max_request_body_size`` is checked once the manager has received the whole
  body, so it limits what the bridge accepts, not what the manager holds. A body
  that is not JSON is rejected with the parse error as soon as the manager finds
  that out, where the HTTP filter answers an oversized one with a 413.
* A request the bridge does not handle is still a declared endpoint's: the
  manager parses and re-serializes its JSON body, where the HTTP filter forwards
  it byte for byte. Declare only the routes MCP clients use.
