.. _metadata_configurations:

Metadata configurations
=======================

Envoy utilizes :ref:`metadata <envoy_v3_api_msg_config.core.v3.Metadata>` to transport arbitrary untyped or typed
data from the control plane to Envoy. Metadata configurations can be applied to Listeners, clusters, routes, virtual hosts,
endpoints, and other elements.


Unlike other configurations, Envoy does not explicitly define the purpose of metadata configurations, which can be used for
stats, logging, or filter/extension behavior. Users can define the purpose of metadata configurations for their specific
use cases. Metadata configurations offer a flexible way to transport user-defined data from the control plane to Envoy without
modifying Envoy's core API or implementation.


For instance, users can add extra attributes to routes, such as the route owner or upstream service maintainer, to metadata.
They can then enable Envoy to log these attributes to the access log or report them to StatsD, among other possibilities.
Moreover, users can write a filter/extension to read these attributes and execute any specific logic.

.. _well_known_metadata:

Well-Known Metadata
-------------------

The following ``typed_filter_metadata`` or ``filter_metadata`` keys are recognized by Envoy and control built-in behavior.
Each entry specifies where the metadata can be configured.

.. _well_known_metadata_envoy_stats_matcher:

``envoy.stats_matcher``
~~~~~~~~~~~~~~~~~~~~~~~

**Type:** :ref:`envoy.config.metrics.v3.StatsMatcher <envoy_v3_api_msg_config.metrics.v3.StatsMatcher>`

**Applicable to:**

- Upstream cluster (:ref:`Cluster.metadata <envoy_v3_api_field_config.cluster.v3.Cluster.metadata>`)
- Listener (:ref:`Listener.metadata <envoy_v3_api_field_config.listener.v3.Listener.metadata>`)

**Fields:** ``typed_filter_metadata``

When present in a cluster's or listener's ``typed_filter_metadata``, Envoy uses the provided
:ref:`StatsMatcher <envoy_v3_api_msg_config.metrics.v3.StatsMatcher>` as the stats matcher for that
resource's stats scope. This per-resource matcher **replaces** (not supplements) the global stats
matcher configured in the bootstrap :ref:`StatsConfig
<envoy_v3_api_msg_config.metrics.v3.StatsConfig>`. Child scopes created under the resource scope
inherit the matcher unless overridden.

This allows fine-grained control over which stats are created per cluster or listener — for example,
enabling a minimal set of stats on high-cardinality resources to reduce memory and CPU overhead.

Cluster example:

.. code-block:: yaml

  clusters:
  - name: my_cluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    metadata:
      typed_filter_metadata:
        envoy.stats_matcher:
          "@type": type.googleapis.com/envoy.config.metrics.v3.StatsMatcher
          inclusion_list:
            patterns:
            - prefix: "cluster.my_cluster.upstream_cx"
    load_assignment:
      cluster_name: my_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 10001

In this example, only stats whose names start with ``cluster.my_cluster.upstream_cx`` are created
for ``my_cluster``, all other cluster stats are suppressed.

Listener example:

.. code-block:: yaml

  listeners:
  - name: my_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 8080
    stat_prefix: my_listener
    metadata:
      typed_filter_metadata:
        envoy.stats_matcher:
          "@type": type.googleapis.com/envoy.config.metrics.v3.StatsMatcher
          inclusion_list:
            patterns:
            - prefix: "listener.my_listener.downstream_cx"
    filter_chains: []

In this example, only stats whose names start with ``listener.my_listener.downstream_cx`` are
created for ``my_listener``, all other listener stats are suppressed.
