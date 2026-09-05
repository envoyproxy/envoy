Added :ref:`header_forwarding
<envoy_v3_api_field_extensions.filters.http.mcp_router.v3.McpRouter.McpBackend.header_forwarding>`
to ``McpBackend``, in both the MCP router filter and the ``mcp_multicluster`` cluster type,
giving per-backend control over which downstream request headers are forwarded upstream. The
default has also changed: previously all downstream request headers were forwarded to every
backend except a small hardcoded skip-list; now, unless a backend explicitly configures
``header_forwarding``, no downstream-controlled headers are forwarded beyond those the router
itself synthesizes -- in particular, the client's ``authorization`` header is no longer
forwarded by default. This matters because a single ``mcp_router`` can aggregate backends of
mixed trust, and MCP requires audience-bound tokens rather than implicit passthrough. Deployments
relying on the previous forward-everything behavior can restore it per backend by setting
``forward_all: true`` on ``header_forwarding``.
