.. _config_http_filters_mcp:

MCP
===

The MCP filter provides Model Context Protocol support for Envoy.

Configuration
-------------

Example configuration:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 67-69
    :lineno-start: 67
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

MCP awareness policy
--------------------

A new filter is introducing the JSON_RPC parser, and we can get the internals to apply some RBAC configs: no_add_client principles cannot call the add tools:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 67-72
    :lineno-start: 67
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

It can also be used with the ext_authz. The MCP filter exports metadata under the ``mcp_proxy`` namespace that can be consumed by the ext_authz filter:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 70-78
    :lineno-start: 70
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Per-Route Configuration
-----------------------

The MCP filter metadata can be used in per-route configuration to enforce policies based on MCP method calls.

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
    :lines: 92-157
    :lineno-start: 92
    :linenos:
    :caption: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`

Full Example
------------

A complete example configuration is available for download: :download:`mcp-filter.yaml <_include/mcp-filter.yaml>`
