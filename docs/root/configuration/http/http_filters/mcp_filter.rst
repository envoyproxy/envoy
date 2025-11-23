.. _config_http_filters_mcp:

MCP
===

The MCP filter provides Model Context Protocol support for Envoy.

Configuration
-------------

Example configuration:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 78-80
    :lineno-start: 78
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

MCP awareness policy
--------------------

A new filter is introducing the MCP JSON_RPC parser, and we can get the internals to apply some RBAC rules based on the MCP attributes:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 78-83
    :lineno-start: 78
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 232-254
    :lineno-start: 232
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

It can also be used with the ext_authz. The MCP filter exports metadata under the ``mcp_proxy`` namespace that can be consumed by the ext_authz filter:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 84-92
    :lineno-start: 84
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Per-Route Configuration
-----------------------

The MCP filter metadata can be used in per-route configuration to enforce policies based on MCP attributes.

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 103-179
    :lineno-start: 103
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Full Example
------------

A complete example configuration is available for download: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`
