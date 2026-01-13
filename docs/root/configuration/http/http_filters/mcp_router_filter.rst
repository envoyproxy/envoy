.. _config_http_filters_mcp_router:

MCP Router
==========

The MCP router filter provides aggregation of multiple Model Context Protocol (MCP) servers.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.mcp_router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp_router.v3.McpRouter
      servers:
      - name: backend1
        mcp_cluster:
          cluster: backend1_cluster
          path: /mcp
      # Optional: specify the metadata namespace to read MCP request info from.
      # Defaults to "mcp_proxy" if not specified.
      metadata_namespace: mcp_proxy
