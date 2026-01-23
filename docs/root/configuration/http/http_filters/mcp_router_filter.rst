.. _config_http_filters_mcp_router:

MCP Router
==========

The MCP router filter provides aggregation of multiple Model Context Protocol (MCP) servers.

This filter must be used together with the :ref:`MCP filter <config_http_filters_mcp>` which parses
incoming MCP requests and populates :ref:`dynamic metadata <config_http_filters_mcp_dynamic_metadata>`
that this filter consumes for routing decisions.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
  - name: envoy.filters.http.mcp_router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_router.v3.McpRouter
      servers:
      - name: backend1
        mcp_cluster:
          cluster: backend1_cluster
          path: /mcp
