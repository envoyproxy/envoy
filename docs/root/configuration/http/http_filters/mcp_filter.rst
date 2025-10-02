.. _config_http_filters_mcp:

MCP
===

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.mcp.v3.Mcp>`

The MCP filter provides Model Context Protocol support for Envoy.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
