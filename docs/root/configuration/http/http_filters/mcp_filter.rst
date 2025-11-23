.. _config_http_filters_mcp:

MCP
===

The MCP filter provides Model Context Protocol support for Envoy.

Configuration
-------------

Example configuration:

.. code-block:: yaml

  http_filters:
  - name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp

MCP awareness policy
--------------------

A new filter is introducing the JSON_RPC parser, and we can get the internals to apply some RBAC configs: no_add_client principles cannot call the add tools:

.. code-block:: yaml

  - name: envoy.filters.http.mcp
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.mcp.v3.Mcp
  - name: envoy.filters.http.rbac
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
      # "Default Deny": Block all requests unless a policy explicitly allows them.
      rules:
        action: DENY
        policies:
          "allow-admin-and-add-tool":
            principals:
              # Use header to as the first step
              - header:
                  name: "x-user-role"      # The header to check
                  string_match:
                    exact: "no_add_client"      # The required role
            permissions:
              - and_rules:
                  rules:
                    - metadata:
                        filter: "mcp_proxy"
                        path:
                          - key: "method"
                        value:
                          string_match:
                            exact: "tools/call"
                    - metadata:
                        filter: "mcp_proxy"
                        path:
                          - key: "params"
                          - key: "name"
                        value:
                          string_match:
                            exact: "add"

It can also be used with the ext_authz.

.. code-block:: yaml

  - name: envoy.filters.http.ext_authz
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
      grpc_service:
        envoy_grpc:
          cluster_name: ext-authz
      metadata_context_namespaces:
      - mcp_proxy

Per-Route Configuration
-----------------------

The MCP filter metadata can be used in per-route configuration to enforce policies based on MCP method calls.

.. code-block:: yaml

  route_config:
    name: local_route
    virtual_hosts:
    - name: local_service
      domains: ["*"]
      routes:
      - match:
          prefix: "/mcp"
        route:
          cluster: mcp_service
        typed_per_filter_config:
          envoy.filters.http.rbac:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBACPerRoute
            rbac:
              rules:
                policies:
                  "allow-lifecycle":
                    permissions:
                    - and_rules:
                        rules:
                        - sourced_metadata:
                            metadata_matcher:
                              filter: "mcp_proxy"
                              path:
                              - key: "method"
                              value:
                                or_match:
                                  value_matchers:
                                  - string_match:
                                      exact: "initialize"
                                  - string_match:
                                      exact: "notifications/initialized"
                                  - string_match:
                                      exact: "ping"
                    principals:
                    - any: true
                  "allow-tool-call":
                    permissions:
                    - and_rules:
                        rules:
                        - sourced_metadata:
                            metadata_matcher:
                              filter: "mcp_proxy"
                              path:
                              - key: "method"
                              value:
                                string_match:
                                  exact: "tools/call"
                    principals:
                    - any: true

Full Example
------------

A complete example configuration showing how to use the MCP filter with RBAC and JWT authentication:

.. literalinclude:: _include/mcp-filter.yaml
    :language: yaml
