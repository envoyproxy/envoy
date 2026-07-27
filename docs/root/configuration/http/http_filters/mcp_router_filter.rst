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
      lazy_initialization: true
      servers:
      - name: backend1
        mcp_cluster:
          cluster: backend1_cluster
          path: /mcp

.. _config_http_filters_mcp_router_lazy_initialization:

Lazy initialization
~~~~~~~~~~~~~~~~~~~

By default, the MCP router eagerly initializes all backend servers during the client's ``initialize``
request, blocking until every backend responds (or times out). When ``lazy_initialization`` is set to
``true``, the ``initialize`` response is returned immediately with gateway capabilities and an empty
backend session map. Each backend is then initialized on-demand when a request first routes to it.

This is useful when some backends are slow or unreliable and should not block client initialization.

.. _config_http_filters_mcp_router_server_requests:

Server-to-client requests
~~~~~~~~~~~~~~~~~~~~~~~~~

The MCP protocol allows backend servers to send requests to clients mid-stream. This includes
``elicitation/create`` (requesting additional input from the user), ``sampling/createMessage``
(requesting LLM completions), and ``roots/list`` (querying available roots).

The MCP router handles these transparently:

1. When a backend sends a server-to-client request via SSE, the gateway forwards it to the client.
   In multiplexing mode (multiple backends), the JSON-RPC ``id`` field is rewritten to include the
   backend name as a prefix (e.g., ``42`` becomes ``"time__42"``), enabling correct routing of the
   client's response.

2. When the client sends a JSON-RPC response back, the gateway parses the prefixed ``id`` to
   determine which backend should receive the response, restores the original ``id`` value, and
   forwards the response to that backend.

In single-backend mode, no ``id`` rewriting is performed since there is only one possible target.

No configuration is required. The gateway advertises ``elicitation`` capability to clients
automatically and handles the request/response routing based on the client's declared capabilities.

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
