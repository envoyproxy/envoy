.. _arch_overview_composite_cluster:

Composite cluster
=================

The composite cluster type enables sophisticated routing and retry strategies across multiple upstream clusters.
It provides a configurable framework for selecting among sub-clusters based on various strategies, with the
initial implementation supporting retry-based progression.

.. note::

  The composite cluster is not a traditional load balancing cluster. It does not perform health checking or
  outlier detection directly. Instead, it delegates to configured sub-clusters, each maintaining their own
  health checking, outlier detection, and load balancing policies.

Overview
--------

The composite cluster acts as a router that selects among multiple configured sub-clusters based on the
operational mode. Each sub-cluster is a fully configured Envoy cluster with its own settings for:

* Load balancing policy
* Health checking
* Outlier detection
* TLS configuration
* Connection pool settings
* Circuit breakers

This design allows heterogeneous cluster configurations within a single logical composite cluster,
enabling advanced use cases like cross-datacenter failover with different security policies per datacenter.

Use cases
---------

**Retry-based Progression**
  Route initial requests to a primary cluster, with retries automatically progressing through
  secondary and tertiary clusters. This enables sophisticated cross-datacenter or cross-region
  failover strategies.

**Future Use-Cases**
  * Weighted distribution across clusters.
  * Session affinity with cluster stickiness.

Configuration
-------------

The composite cluster is configured using the
:ref:`ClusterConfig <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>` message.

The configuration requires:

1. A list of ``sub_clusters`` referencing existing cluster names
2. Mode-specific configuration (e.g., ``retry_config`` for RETRY mode)

The operational mode is determined by which configuration is provided (e.g., setting ``retry_config`` enables RETRY mode).

Basic example
~~~~~~~~~~~~~

.. code-block:: yaml

  static_resources:
    clusters:
    - name: composite_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED  # Required for composite clusters
      cluster_type:
        name: envoy.clusters.composite
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
          sub_clusters:
          - name: primary_cluster
          - name: secondary_cluster
          retry_config:
            overflow_option: FAIL

    # Define the sub-clusters
    - name: primary_cluster
      type: STRICT_DNS
      connect_timeout: 0.25s
      load_assignment:
        cluster_name: primary_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: primary.example.com
                  port_value: 443

    - name: secondary_cluster
      type: STRICT_DNS
      connect_timeout: 0.25s
      load_assignment:
        cluster_name: secondary_cluster
        endpoints:
        - lb_endpoints:
          - endpoint:
              address:
                socket_address:
                  address: secondary.example.com
                  port_value: 443

Retry mode
----------

In ``RETRY`` mode, the composite cluster selects sub-clusters based on the retry attempt number:

* **Initial request** (Attempt #1): Routes to the first sub-cluster.
* **First retry** (Attempt #2): Routes to the second sub-cluster.
* **Second retry** (Attempt #3): Routes to the third sub-cluster.
* **Further retries**: Behavior determined by ``overflow_option``.

The retry progression works in conjunction with Envoy's
:ref:`retry policies <envoy_v3_api_field_config.route.v3.RetryPolicy.retry_on>`. The route configuration
determines what constitutes a retriable failure (5xx, reset, etc.).

Configuration example
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: yaml

  static_resources:
    clusters:
    - name: multi_region_cluster
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cluster_type:
        name: envoy.clusters.composite
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
          name: "composite_cluster"
          sub_clusters:
          - name: us_east_cluster
          - name: us_west_cluster
          - name: eu_west_cluster
          retry_config:
            overflow_option: USE_LAST_CLUSTER
            honor_route_retry_policy: true

  # Route configuration with retry policy
  http_filters:
  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  route_config:
    virtual_hosts:
    - name: backend
      domains: ["*"]
      routes:
      - match:
          prefix: "/"
        route:
          cluster: multi_region_cluster
          retry_policy:
            retry_on: "5xx,reset,connect-failure,retriable-4xx"
            num_retries: 5
            retry_host_predicate:
            - name: envoy.retry_host_predicates.previous_hosts

Connection lifetime callbacks
-----------------------------

The composite cluster aggregates connection lifetime callbacks from all sub-clusters, providing a
unified interface for monitoring connection events across the entire cluster set. This ensures that
connection pool metrics and observability features work seamlessly regardless of which sub-cluster
is selected.
