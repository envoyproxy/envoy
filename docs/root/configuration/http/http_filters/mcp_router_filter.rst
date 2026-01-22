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

Metadata Namespace
~~~~~~~~~~~~~~~~~~

The MCP router reads MCP request metadata from the upstream :ref:`MCP filter <config_http_filters_mcp>`.
By default, it reads from the ``envoy.filters.http.mcp`` namespace.

To use the legacy ``mcp_proxy`` namespace, disable the runtime guard
``envoy.reloadable_features.mcp_filter_use_new_metadata_namespace``.

You can also explicitly configure the namespace using the ``metadata_namespace`` field:

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
      metadata_namespace: envoy.filters.http.mcp
