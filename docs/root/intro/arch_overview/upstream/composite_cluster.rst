.. _arch_overview_composite_cluster:

Composite cluster
=================

The composite cluster type provides retry-aware cluster selection, allowing different retry attempts
to automatically target different upstream clusters. Unlike the standard
:ref:`aggregate cluster <arch_overview_aggregate_cluster>` which uses health-based selection, the
composite cluster uses the retry attempt count to deterministically select which sub-cluster to route to.

Use cases
---------

The composite cluster addresses several important scenarios:

* **Retry-based progression**: Different clusters for retry attempts (primary → secondary → tertiary).
* **AI Gateway failover**: Route initial requests to preferred providers and retries to fallbacks.
* **Cost optimization**: Try expensive, high-performance services first, fall back to cheaper alternatives.

Configuration
-------------

The composite cluster is configured using the
:ref:`ClusterConfig <envoy_v3_api_msg_extensions.clusters.composite.v3.ClusterConfig>`.

Example configuration
~~~~~~~~~~~~~~~~~~~~~

The following example shows a composite cluster with three sub-clusters:

.. code-block:: yaml

  name: composite_cluster
  connect_timeout: 0.25s
  lb_policy: CLUSTER_PROVIDED
  cluster_type:
    name: envoy.clusters.composite
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
      clusters:
      - name: primary_cluster
      - name: secondary_cluster
      - name: fallback_cluster

In this configuration:

- Initial requests (attempt 1) go to ``primary_cluster``.
- First retries (attempt 2) go to ``secondary_cluster``.
- Second retries (attempt 3) go to ``fallback_cluster``.
- Further retry attempts (attempt 4+) will fail with no host available.

Cluster selection
-----------------

The composite cluster uses a sequential selection strategy based on retry attempt count:

* **Initial request** (attempt 1): Uses the first cluster.
* **First retry** (attempt 2): Uses the second cluster.
* **Second retry** (attempt 3): Uses the third cluster.
* **Further retries**: Fail with no host available.

When retry attempts exceed the length of the order in effect for the request, requests fail with
no host available. With no per-request order that length is the number of configured clusters, so
configure the number of retries in your retry policy to match your cluster configuration.

Dynamic order
-------------

The order above is fixed at configuration time and is the same for every request. Setting
:ref:`order_metadata_key
<envoy_v3_api_field_extensions.clusters.composite.v3.ClusterConfig.order_metadata_key>` additionally
lets a filter choose the order for a single request, while configuration keeps ownership of the set
of clusters a request may reach:

.. code-block:: yaml

  cluster_type:
    name: envoy.clusters.composite
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.clusters.composite.v3.ClusterConfig
      clusters:
      - name: primary_cluster
      - name: secondary_cluster
      - name: fallback_cluster
      order_metadata_key:
        key: envoy.clusters.composite
        path:
        - key: order

A filter earlier in the chain, such as :ref:`ext_proc <config_http_filters_ext_proc>` or
:ref:`set_metadata <config_http_filters_set_metadata>`, writes a list of cluster names at that key:

.. code-block:: yaml

  envoy.clusters.composite:
    order: ["fallback_cluster", "primary_cluster"]

.. warning::

  Do not store the order in the ``envoy.lb`` metadata namespace. The router reserves it: every
  field a request carries under ``envoy.lb`` is merged into the load balancer's
  :ref:`metadata match criteria <envoy_v3_api_field_config.route.v3.RouteAction.metadata_match>`,
  so an order list stored there becomes a match criterion that no host can satisfy, and requests
  to sub-clusters using the :ref:`subset load balancer <arch_overview_load_balancer_subsets>`
  fail with no healthy upstream.

The effective order for the request is that list with any entry that does not name a configured
cluster removed. In the example above, attempt 1 goes to ``fallback_cluster`` and attempt 2 to
``primary_cluster``; attempt 3 fails with no host available, because the effective order has only
two entries. A cluster may be named more than once, in which case it is used for more than one
attempt.

The rules are:

* **Configuration is the allowlist**: entries naming a cluster that is not in ``clusters`` are
  ignored, so a request can never reach a cluster the configuration did not authorize.
* **Fallback is the configured order**: if the key is absent, does not hold a list, or no entry
  names a configured cluster, the configured ``clusters`` order is used. Malformed metadata never
  fails a request.
* **Overflow is unchanged**: attempts past the end of the effective order fail with no host
  available, exactly as they do past the end of the configured order. The supplied order replaces
  the configured one rather than prefixing it. Note that a list containing repeated names can be
  longer than ``clusters``, and then permits more attempts than there are configured clusters.
* **Indexed by attempt number**: the list is read again on each attempt and indexed by that
  attempt's number, not consumed entry by entry. A filter that modifies the list between retries
  must leave the already-consumed prefix in place and only append or replace later entries;
  removing consumed entries shifts every remaining entry forward and ends the sequence early.
* **Still health-blind**: as with the configured order, selection does not consider sub-cluster
  health. An entry naming a cluster with no healthy hosts still consumes its attempt.

Retry policy coordination
-------------------------

For the composite cluster to function correctly, configure an appropriate
:ref:`retry policy <envoy_v3_api_msg_config.route.v3.RetryPolicy>` at the route level:

.. code-block:: yaml

  retry_policy:
    retry_on: "5xx,gateway-error,connect-failure,refused-stream"
    num_retries: 2  # Enables attempts 1, 2, and 3 (3 total attempts)

Important considerations
------------------------

* **Sub-cluster independence**: Each sub-cluster maintains its own health checking, load balancing,
  and outlier detection. If a selected sub-cluster has no healthy hosts available, the request will
  fail according to that sub-cluster's load balancing behavior, potentially triggering another retry
  attempt if configured.
* **Deterministic routing**: The same retry attempt will always target the same cluster given
  identical configuration and, where used, identical order metadata. Cluster selection is based
  solely on retry attempt count, not on the health status of sub-clusters.
* **Thread-local clustering**: Cluster selection occurs at the thread-local level for optimal
  performance.
* **Sub-cluster health**: Unlike the aggregate cluster, the composite cluster does not consider
  sub-cluster health when selecting which cluster to use. Each retry attempt targets a specific
  cluster based on attempt count, regardless of whether that cluster has healthy endpoints available.

Comparison with aggregate cluster
---------------------------------

+------------------+---------------------------+-----------------------------------------+
| Feature          | Aggregate Cluster         | Composite Cluster                       |
+==================+===========================+=========================================+
| Selection basis  | Health status             | Retry attempt count                     |
+------------------+---------------------------+-----------------------------------------+
| Primary use case | Health-based failover     | Retry progression                       |
+------------------+---------------------------+-----------------------------------------+
| Order source     | Configuration             | Configuration, optionally overridden    |
|                  |                           | per request via dynamic metadata        |
+------------------+---------------------------+-----------------------------------------+
| Overflow handling| Health-dependent          | Fails request                           |
+------------------+---------------------------+-----------------------------------------+
| Predictability   | Health-dependent          | Fully deterministic                     |
+------------------+---------------------------+-----------------------------------------+


