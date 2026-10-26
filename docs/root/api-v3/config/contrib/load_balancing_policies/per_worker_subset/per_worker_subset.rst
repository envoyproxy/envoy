.. _extension_envoy.load_balancing_policies.per_worker_subset:

Per-worker subset load balancer
===============================

* This load balancer should be configured with the type URL
  ``type.googleapis.com/envoy.extensions.load_balancing_policies.per_worker_subset.v3alpha.PerWorkerSubset``.

.. note::

  Per-worker subset is a contributed extension that must be explicitly enabled at Envoy build
  time. See :ref:`install_contrib` for details.

Each Envoy worker maintains its own subset of upstream hosts and load balances requests only
across that subset. This caps per-worker connection-pool fanout for large upstream clusters while
preserving fleet-wide host coverage.

The policy is configured with the
``envoy.extensions.load_balancing_policies.per_worker_subset.v3alpha.PerWorkerSubset`` message.
