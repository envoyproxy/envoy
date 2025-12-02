.. _config_http_filters_mcp_router:

MCP Router
==========

The MCP router filter provides aggregatrion of multiple Model Context Protocol (MCP) servers.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.mcp_router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.McpRouter
