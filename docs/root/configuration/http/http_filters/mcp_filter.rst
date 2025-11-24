.. _config_http_filters_mcp:

MCP
===

The MCP filter provides Model Context Protocol support for Envoy.
The MCP filter will accept MCP JSON_RPC and extract the MCP methods to make Envoy aware of the MCP attributes.

Example usage:
--------------

Put the MCP filter before the RBAC filter to apply some RBAC rules based on the MCP attributes:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 78-84
    :lineno-start: 78
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

The RBAC filter is configured with a per-route configuration to enforce policies based on MCP attributes:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 234-257
    :lineno-start: 234
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

The MCP filter can also be used with ext_authz.

The MCP filter exports metadata under the ``mcp_proxy`` namespace by default, which can be consumed by the ext_authz filter:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 85-93
    :lineno-start: 85
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Full Example
------------

A complete example configuration is available for download: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`
