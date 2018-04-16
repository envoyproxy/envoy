.. _config_http_filters_router_v1:

Router
======

Router :ref:`configuration overview <config_http_filters_router>`.

.. code-block:: json

  {
    "name": "router",
    "config": {
      "dynamic_stats": "...",
      "start_child_span": "..."
    }
  }

dynamic_stats
  *(optional, boolean)* Whether the router generates :ref:`dynamic cluster statistics
  <config_cluster_manager_cluster_stats_dynamic_http>`. Defaults to *true*. Can be disabled in high
  performance scenarios.

.. _config_http_filters_router_start_child_span:

start_child_span
  *(optional, boolean)* Whether to start a child :ref:`tracing <arch_overview_tracing>` span for
  egress routed calls. This can be useful in scenarios where other filters (auth, ratelimit, etc.)
  make outbound calls and have child spans rooted at the same ingress parent. Defaults to *false*.
