.. _arch_overview_mcp_multicluster:

MCP multi cluster
=================

The MCP multi cluster combines multiple clusters into one, with the primary purpose of aggregating multiple
MCP servers into one. It is intended to be used for routes with the `MCP router <_config_http_filters_mcp_router>`
as the terminal filter.

MCP multi cluster provides metadata at the well defined key ``envoy.clusters.mcp_multicluster`` with the
`envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>`
protobuf. The MCP router extension uses this metadata to discover tools and resources implemented by aggregated MCP servers.

Configuration
-------------

The MCP multi cluster is configured using the
:ref:`ClusterConfig <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>`.

Example configuration
~~~~~~~~~~~~~~~~~~~~~

The following example shows a MCP multi cluster that aggregates two MCP servers:

.. code-block:: yaml

  name: mcp_multicluster
  connect_timeout: 0.25s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.composite
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
      servers:
      - name: build_tools
        cluster: build_tools
      - name: review_tools
        cluster: build_tools
        host_rewrite_literal: "mcp.review_tools.acme.com"

In this configuration Envoy acts as an MCP server aggregating all tools and resources implemented by the
``build_tools`` and the ``review_tools`` MCP servers.
