.. _config_http_cluster_specifier_header_weighted:

Header weighted cluster specifier
================================

Overview
--------

The header weighted cluster specifier plugin selects the target upstream
cluster by combining header-based explicit selection with weighted random
choice.

When the configured request header is present and non-empty, its value is
used directly as the target cluster name. This allows an external system
(e.g., an API gateway, service mesh control plane, or routing policy engine)
to control the upstream target via a request header such as
``x-target-cluster``.

When the header is absent or empty, the plugin falls back to weighted random
selection among the configured clusters, using the same algorithm as the
built-in :ref:`weighted_clusters <config_http_conn_man_route_table_traffic_splitting_split>`.
If no cluster can be selected (e.g., the total weight is zero), the
configured ``default_cluster`` is used.

Configuration
-------------

* This plugin should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted>`

A simple example configuration:

.. code-block:: yaml

  route_config:
    routes:
    - match: { prefix: "/" }
      route:
        cluster_specifier_plugin:
          name: envoy.router.cluster_specifier_plugin.header_weighted
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted
            header: x-target-cluster
            clusters:
            - name: cluster-a
              weight: 50
            - name: cluster-b
              weight: 50
            default_cluster: cluster-a

Behavior
--------

The plugin selects the target cluster in the following order:

1. **Header takes precedence:** When the
   :ref:`header <envoy_v3_api_field_extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted.header>`
   is present and non-empty, its value is used directly as the target cluster
   name. Weighted selection is skipped.
2. **Weighted fallback:** When the header is absent or empty, a cluster is
   selected from the
   :ref:`clusters <envoy_v3_api_field_extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted.clusters>`
   list by weighted random choice. Each entry's
   :ref:`weight <envoy_v3_api_field_extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted.ClusterWeight.weight>`
   determines its selection probability relative to the total weight.
3. **Default cluster fallback:** When no cluster can be selected (e.g., the
   total weight is zero), the
   :ref:`default_cluster <envoy_v3_api_field_extensions.router.cluster_specifiers.header_weighted.v3.HeaderWeighted.default_cluster>`
   is used.

All cluster names in the ``clusters`` list and the ``default_cluster`` are
validated for existence in the cluster manager configuration at config load
time. If any cluster does not exist, the configuration is rejected (NACK).
