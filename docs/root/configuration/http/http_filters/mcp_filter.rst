.. _config_http_filters_mcp:

MCP
===

The MCP filter provides Model Context Protocol support for Envoy.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
