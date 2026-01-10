.. _config_http_filters_mcp:

Model Context Protocol (MCP)
============================

The MCP HTTP filter enables native Model Context Protocol support within Envoy.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.mcp.v3.Mcp>`

.. attention::

   The MCP filter is actively under development.

This filter allows Envoy to function as an MCP gateway, enabling two deployment patterns:

Pass-Through MCP Gateway
~~~~~~~~~~~~~~~~~~~~~~~~

Envoy's primary role is a Policy Enforcement Point (PEP) for policies defined in either HTTP or MCP formats.

* It supports Streamable-HTTP transport.
* Service selection is handled via standard *virtual host (vhost) / route configuration* or through a *dynamic forwarding proxy*.

Aggregating MCP mode
~~~~~~~~~~~~~~~~~~~~

.. attention::

   This functionality is pending with the multi-route filter.

Envoy functions as a unified aggregating MCP server.

* It combines the capabilities, tools, and resources of multiple backend MCP servers and presents them to clients as a single logical MCP server.
* It supports Streamable-HTTP transport.

Key Capabilities
~~~~~~~~~~~~~~~~

Within these patterns, the filter facilitates three essential functions:

* **MCP Policy Enforcement**: Extracts MCP attributes to enforce fine-grained access control using either RBAC or an external authorization service.
* **MCP Observability**: Extracts MCP attributes to populate dynamic metadata, which is then consumed by access logs or tracers for enhanced monitoring and debugging.
* **MCP Multiplexing and Aggregation**: Acts as a unified endpoint that aggregates tools and resources originating from multiple backend services (Feature *Pending*).

MCP Policy Enforcement Examples
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A common usage of the MCP filter is to enforce policies based on MCP payload attributes. The filter parses MCP JSON_RPC messages and populates
the dynamic metadata, which subsequent filters in the chain can use for decision-making.

This enables scenarios such as:

* **Per-route Policy**: Applying specific RBAC rules for different routes or MCP methods.
* **Egress Traffic Control**: Using the filter with a dynamic forward proxy to secure outbound traffic for AI agents.

Integration with RBAC
---------------------

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
    :emphasize-lines: 13-21
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Integration with External Authorization
---------------------------------------

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
