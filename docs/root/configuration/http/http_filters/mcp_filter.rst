.. _config_http_filters_mcp:

Model Context Protocol (MCP)
============================

The MCP HTTP filter enables native Model Context Protocol support within Envoy.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.mcp.v3.Mcp>`

.. attention::

   The MCP filter is actively under development.

This filter enables Envoy to act as an intelligent MCP gateway, facilitating two primary patterns:

1.  **Identity-Aware Policy Enforcement:** Extracting MCP attributes to enforce fine-grained access control via RBAC or external authorization.
2.  **Multiplexing and Aggregation:** Acting as a unified endpoint that aggregates tools and resources from multiple backend services (Pending).

Identity and Policy Enforcement
-------------------------------

A common usage of the MCP filter is to enforce policies based on MCP payload attributes. The filter parses MCP messages and populates dynamic metadata,
which subsequent filters in the chain can use for decision-making.

This enables scenarios such as:
* **Per-route Policy:** Applying specific RBAC rules for different routes or MCP methods.
* **Egress Traffic Control:** Using the filter with a dynamic forward proxy to secure outbound traffic for AI agents.

Integration with RBAC
~~~~~~~~~~~~~~~~~~~~~

To apply RBAC rules based on MCP attributes, place the MCP filter before the RBAC filter in the HTTP connection manager chain:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 78-84
    :lineno-start: 78
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

The RBAC filter is then configured with a per-route policy to match against the metadata extracted by the MCP filter:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 234-256
    :lineno-start: 234
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Integration with External Authorization
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The MCP filter can also function alongside the ``ext_authz`` filter. By default, the MCP filter exports metadata under the ``mcp_proxy`` namespace. An external authorization service can evaluate this metadata to approve or deny requests.

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 85-93
    :lineno-start: 85
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Full Example
------------

A complete example configuration is available for download: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`