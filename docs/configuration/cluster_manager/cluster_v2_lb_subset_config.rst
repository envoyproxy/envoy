.. _config_cluster_manager_cluster_v2_lb_subset_config:

Load Balancer Subset Configuration
==================================

* Load balancer subsets :ref:`architecture overview <arch_overview_load_balancer_subsets>`.
* If load balancer subsets are configured for a cluster, additional statistics are emitted. They are
  documented :ref:`here <config_cluster_manager_cluster_stats>`.

.. code-block:: json

  {
    "fallback_policy": "...",
    "default_subset": "{...}",
    "subset_selectors": "[...]"
  }

.. _config_cluster_manager_cluster_v2_lb_subset_config_fallback_policy:

fallback_policy
  *(required, string)* The fallback policy if no subset matching the route's criteria exists.
  Possible values are *NO_FALLBACK*, *ANY_ENDPOINT*, or *DEFAULT_SUBSET*. *NO_FALLBACK* results in
  request failure. *ANY_ENDPOINT* fallback means that load balancing occurs over all cluster hosts,
  without regard to metadata. *DEFAULT_SUBSET* fallback load balances among hosts matching the
  metadata in the *default_subset* field.

.. _config_cluster_manager_cluster_v2_lb_subset_config_default_subset:

default_subset
  *(optional, object)* A map of key/value pairs that hosts must match to be considered part of
  fallback to a default subset. If *fallback_policy* is *DEFAULT_SUBSET* and this field is empty,
  the subset load balancer behave as if the fallback policy is *ANY_ENDPOINT*.

.. _config_cluster_manager_cluster_v2_lb_subset_config_subset_selectors:

`subset_selectors <config_cluster_manager_cluster_v2_lb_subset_config_subset_selectors_detail>`
  *(required, array)* An array of subset selectors. The :ref:`architecture overview
  <arch_overview_load_balancer_subsets>` describes how these selectors are converted into subsets.
  Order does not matter.

.. _config_cluster_manager_cluster_v2_lb_subset_config_subset_selectors_detail:

Subset Selectors
----------------

.. code-block:: json

  {
    "keys": "[...]"
  }

.. _config_cluster_manager_cluster_v2_lb_subset_config_subset_selectors_keys:

keys
  *(required, array)* An array of strings specifying the keys used to generate subsets.
  Order does not matter.
