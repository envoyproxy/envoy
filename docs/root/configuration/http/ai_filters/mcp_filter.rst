.. _config_http_ai_filters_mcp:
.. _extension_envoy.http.ai_filters.mcp:

MCP
===

The MCP AI filter (``envoy.http.ai_filters.mcp``) inspects Model Context Protocol
requests: it classifies a request as MCP traffic, holds it to the configured
:ref:`traffic mode <envoy_v3_api_field_extensions.filters.http.mcp.v3.Mcp.traffic_mode>`,
and publishes its attributes for routing and policy.

It runs the inspection of the :ref:`MCP HTTP filter <config_http_filters_mcp>`
and takes its configuration, so the two behave alike and a feature of one is a
feature of the other. Here the manager holds the request and parses it once for
every AI filter of the chain.

* This AI filter should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.mcp.v3.Mcp>`

Configuration
-------------

Declare the routes that carry MCP requests as AI endpoints; they need no
``llm_protocol``.

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling:
        reserialize_body: DISABLE
      filters:
      - name: envoy.http.ai_filters.mcp
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
          traffic_mode: REJECT_NO_MCP

  routes:
  - match:
      path: /mcp
    route:
      cluster: mcp_backend
    typed_per_filter_config:
      envoy.filters.http.ai_protocol_manager:
        "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute
        request: {}

The attributes are published as the HTTP filter publishes them, in the
``envoy.filters.http.mcp`` dynamic metadata namespace and the
``envoy.filters.http.mcp.request`` filter state object, so routes and policies
written against one work with the other. What differs is where the inspection
is hosted:

* A route's :ref:`McpOverride
  <envoy_v3_api_msg_extensions.filters.http.mcp.v3.McpOverride>` goes in the
  manager's :ref:`typed_per_filter_config
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.AiProtocolManagerPerRoute.typed_per_filter_config>`
  under the filter's name.
* The manager parses the body, and has no document for one that is not JSON,
  that ends early, or that repeats a key. The filter rejects such a body in
  ``REJECT_NO_MCP``. Otherwise it records ``passthrough_reason`` and the request
  is forwarded as it arrived, as the HTTP filter forwards it, without the
  attributes the HTTP filter reads before the body fails. A body that repeats a
  key is handled this way whatever ``reject_duplicate_keys`` says.
* :ref:`max_request_body_size
  <envoy_v3_api_field_extensions.filters.http.mcp.v3.Mcp.max_request_body_size>`
  limits the request the filter accepts, not what is buffered. A longer request
  is handled as the HTTP filter handles one it parsed in part: rejected in
  ``REJECT_NO_MCP``, and marked ``is_exceeding_limit`` otherwise. Its attributes
  are those of the whole body, past the limit too. With
  :ref:`early_terminate_when_routable
  <envoy_v3_api_field_extensions.filters.http.mcp.v3.Mcp.early_terminate_when_routable>`,
  a longer request that has all of its routing attributes is not over the limit.
* The filter raises the manager's :ref:`inline_string_threshold_bytes
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestParsingLimits.inline_string_threshold_bytes>`
  to ``max_request_body_size``, at most 1MiB, on every route of the manager. A
  request with an attribute in a longer string is handled as one over the limit;
  longer strings the filter does not read, such as tool arguments, are not.
* The filter only reads the request, so :ref:`reserialize_body
  <envoy_v3_api_field_extensions.filters.http.ai_protocol_manager.v3.RequestHandling.reserialize_body>`
  can be ``DISABLE`` unless another AI filter edits it.
* A 2xx JSON or SSE response to a request the filter inspected is parsed and
  re-serialized by the manager on its way back, as for any AI filter: its JSON
  is equivalent, not byte for byte.

Statistics
----------

The filter outputs statistics in the ``ai_protocol_manager.mcp.`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  requests_rejected, Counter, The filter rejected a request that is not MCP traffic.
  invalid_json, Counter, The filter saw a request body that is not JSON.
  body_too_large, Counter, The filter rejected a request over its body size limit.
  duplicate_keys_rejected, Counter, Unused: the manager rejects a body that repeats a key before the filter runs.
  header_mismatch, Counter, The MCP headers of a request disagreed with its body.
