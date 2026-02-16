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

.. _config_http_filters_mcp_router_statistics:

Statistics
----------

The MCP router filter outputs statistics in the ``<stat_prefix>.mcp_router.`` namespace.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  rq_total, Counter, Total MCP requests processed
  rq_fanout, Counter, Requests fanned out to multiple backends
  rq_direct_response, Counter, "Requests handled locally (e.g., ping, notifications)"
  rq_body_rewrite, Counter, Requests where the body was rewritten (tool/prompt/URI prefix stripping)
  rq_invalid, Counter, Requests rejected due to invalid or missing metadata or unsupported method
  rq_unknown_backend, Counter, Requests where the target backend could not be resolved
  rq_backend_failure, Counter, Requests where a single backend returned an error
  rq_fanout_failure, Counter, Fanout requests where all backends failed
  rq_session_invalid, Counter, Requests with an invalid or unparseable session ID
  rq_auth_failure, Counter, Requests rejected due to session identity validation failure
