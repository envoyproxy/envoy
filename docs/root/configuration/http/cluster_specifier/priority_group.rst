.. _config_http_cluster_specifier_priority_group:

Priority group cluster specifier
================================

Overview
--------

The priority group cluster specifier splits the candidate clusters of a route into an ordered list
of named groups and picks the group by the attempt count of the request: the initial attempt uses
the first group, the first retry uses the second group, and so on. The target cluster is then
selected from the clusters of the chosen group based on the cluster weights.

In other words, the group list is a **cross-cluster fallback chain** and every group is the set of
clusters that is acceptable for one attempt of the request.

.. _config_http_cluster_specifier_priority_group_motivation:

Why this is needed
------------------

Envoy already provides several ways to keep a request alive when an upstream is unhealthy, but most
of them work *inside* a single cluster or are fixed for the whole request:

* :ref:`Priority levels <arch_overview_load_balancing_priority_levels>` and
  :ref:`locality weights <arch_overview_load_balancing_locality_weighted_lb>` fail over between the
  endpoints of one single cluster.
* :ref:`Weighted clusters <envoy_v3_api_msg_config.route.v3.WeightedCluster>` split the traffic
  across clusters, but all attempts try the same cluster list. All clusters in the list are considered
  for every attempt.

But in the practice, it's possible for a users to want different cluster set to be tried on a specific
order of the attempts.

For example, the AI gateway may want to try local model providers first and only fall back to remote
providers if the initial attempt fails. And multiple different local model providers may have different
capabilities, performance characteristics, or availability, which makes it desirable to distribute
traffic among them with specific weights.

How it works
------------

Group selection
~~~~~~~~~~~~~~~

The group of an attempt is selected by the attempt index, that is the attempt count of the request
minus one (the attempt count is 1 for the initial attempt, 2 for the first retry, and so on):

.. code-block:: text

  group = priority_groups[min(attempt_count - 1, len(priority_groups) - 1)]

so the initial attempt uses ``priority_groups[0]``, the first retry uses ``priority_groups[1]``, and
so on. Once the attempts go past the end of the list, the request stays on the last group for all
the remaining attempts.

And it's possible to repeat a group in the list to make it receive multiple attempts consecutively.
That is how a "try the primary provider twice, then fall back" policy is expressed in the priority
group list.

Cluster selection
~~~~~~~~~~~~~~~~~

Within the selected group the target cluster is picked from the cluster weights, in the same way as
:ref:`weighted clusters <envoy_v3_api_field_config.route.v3.WeightedCluster.clusters>`. The random
value that drives the pick is by default the internally generated random value of the request. It
can instead be read from a request header with :ref:`header_name
<envoy_v3_api_field_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier.header_name>`
or be generated from the :ref:`hash policies
<envoy_v3_api_field_config.route.v3.RouteAction.hash_policy>` of the route with
:ref:`use_hash_policy
<envoy_v3_api_field_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier.use_hash_policy>`,
to get a stable pick across multiple proxy levels.

The random value is computed once per request and the same value is reused for all the attempts, so
the retries of a request keep a consistent position in the weight intervals of every group.

Per-request groups
~~~~~~~~~~~~~~~~~~

The group list can be overridden per request by an optional dynamic metadata namespace, see
:ref:`override_metadata_namespace
<envoy_v3_api_field_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier.override_metadata_namespace>`.
The value of the namespace is a :ref:`PriorityGroupsOverride
<envoy_v3_api_msg_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupsOverride>`
message: it is read from the :ref:`typed dynamic metadata
<envoy_v3_api_field_config.core.v3.Metadata.typed_filter_metadata>` first and from the
:ref:`untyped one <envoy_v3_api_field_config.core.v3.Metadata.filter_metadata>`, as a struct of the
same shape, if the namespace is not present there.

An overriding group that only carries a ``name`` selects one of the configured groups by name and
keeps its configured clusters and weights; a group that also carries ``clusters`` replaces them for
the current request. This makes the fallback chain a per-request decision that an earlier filter
computes from the request, while the clusters themselves stay in the static configuration.

.. note::

  The route, and with it the cluster of the initial attempt, is resolved before the HTTP filter
  chain runs. A filter that writes the metadata has to clear the route cache as well.

Requirements
------------

* The route must enable :ref:`refresh_cluster_on_retry
  <envoy_v3_api_field_config.route.v3.RetryPolicy.refresh_cluster_on_retry>` in its retry policy.
  Without it the retry reuses the cluster of the initial attempt and every group after the first one
  is dead configuration.
* The retry policy must actually retry the failures that should move the request to the next group,
  for example by adding ``retriable-status-codes`` with
  :ref:`retriable_status_codes <envoy_v3_api_field_config.route.v3.RetryPolicy.retriable_status_codes>`
  for the overload responses of an upstream.

Configuration
-------------

* This cluster specifier should be configured with the type URL ``type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier>`

Example scenarios
-----------------

Cross-provider fallback
~~~~~~~~~~~~~~~~~~~~~~~

An AI gateway sends a request to the preferred provider and falls back to the alternatives when the
preferred one is overloaded or unreachable. Each provider is one group, the preferred one splits
its traffic across two model clusters, and ``num_retries`` is one less than the number of the groups
so that the chain is walked exactly once.

.. code-block:: yaml

  name: local_route
  virtual_hosts:
  - name: llm_gateway
    domains: ["*"]
    routes:
    - match:
        prefix: "/v1/messages"
      route:
        inline_cluster_specifier_plugin:
          extension:
            name: envoy.router.cluster_specifier_plugin.priority_group
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier
              priority_groups:
              - name: provider_a
                clusters:
                - cluster_name: provider_a_model_1
                  weight: 80
                - cluster_name: provider_a_model_2
                  weight: 20
              - name: provider_b
                clusters:
                - cluster_name: provider_b
                  weight: 100
              - name: provider_c
                clusters:
                - cluster_name: provider_c
                  weight: 100
        retry_policy:
          retry_on: 5xx,reset,connect-failure,retriable-status-codes
          retriable_status_codes: [429]
          num_retries: 2
          refresh_cluster_on_retry: true

The initial attempt stays with the preferred provider and is split 80/20 across its two model
clusters. A ``429``, a ``5xx`` or a connection failure moves the first retry to ``provider_b`` and
the second retry to ``provider_c``.

Several attempts per group
~~~~~~~~~~~~~~~~~~~~~~~~~~

The same clusters may be listed more than once to spend more than one attempt on them. Here the
primary provider is tried twice before the request falls back:

.. code-block:: yaml

  priority_groups:
  - name: provider_a
    clusters:
    - cluster_name: provider_a
      weight: 100
  - name: provider_a
    clusters:
    - cluster_name: provider_a
      weight: 100
  - name: provider_b
    clusters:
    - cluster_name: provider_b
      weight: 100

Weighted capacity with an overflow
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The groups do not have to hold a single cluster. Here the on-premise capacity is split 80/20 across
two clusters and the whole group falls back to a cloud provider:

.. code-block:: yaml

  priority_groups:
  - name: on_premise
    clusters:
    - cluster_name: on_premise_primary
      weight: 80
    - cluster_name: on_premise_secondary
      weight: 20
  - name: cloud
    clusters:
    - cluster_name: cloud_provider
      weight: 100

The initial attempt is routed to ``on_premise_primary`` or ``on_premise_secondary`` with an 80/20
split, and the retry is routed to ``cloud_provider``.

Per-request fallback chain
~~~~~~~~~~~~~~~~~~~~~~~~~~

An AI gateway usually cannot use one chain for all the traffic: the acceptable providers depend on
the requested model, on the tenant or on the API key. A filter that runs before the router, for
example :ref:`ext_proc <config_http_filters_ext_proc>` or :ref:`Lua <config_http_filters_lua>`,
resolves the chain, writes it to the dynamic metadata and clears the route cache; the cluster
specifier is configured to read it:

.. code-block:: yaml

  "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupClusterSpecifier
  override_metadata_namespace: envoy.my_filter
  priority_groups:
  - name: provider_a
    clusters:
    - cluster_name: provider_a
      weight: 100
  - name: provider_b
    clusters:
    - cluster_name: provider_b
      weight: 100

The metadata that the filter writes for a request that should prefer ``provider_b`` with a 20/80
split over two of its clusters and fall back to the configured ``provider_a`` group:

.. code-block:: yaml

  envoy.my_filter:
    priority_groups:
    - name: provider_b
      clusters:
      - cluster_name: provider_b
        weight: 20
      - cluster_name: provider_b_backup
        weight: 80
    - name: provider_a

The same override can be published as typed dynamic metadata instead, which carries the
``PriorityGroupsOverride`` message itself:

.. code-block:: yaml

  envoy.my_filter:
    "@type": type.googleapis.com/envoy.extensions.router.cluster_specifiers.priority_group.v3.PriorityGroupsOverride
    priority_groups:
    - name: provider_b
      clusters:
      - cluster_name: provider_b
        weight: 20
      - cluster_name: provider_b_backup
        weight: 80
    - name: provider_a

If the metadata namespace is missing, cannot be parsed as a ``PriorityGroupsOverride`` message,
holds no group at all, or its group for the current attempt is not a valid override, the configured
``priority_groups`` are used instead, so a filter failure degrades to the static chain rather than
to a failed request.
