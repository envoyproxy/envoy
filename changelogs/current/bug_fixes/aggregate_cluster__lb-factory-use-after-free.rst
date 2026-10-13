Fixed a use-after-free crash in the :ref:`aggregate cluster <arch_overview_aggregate_cluster>`
load balancer factory. The factory is shared with every worker thread but held a raw reference to
its ``Cluster``, which a CDS update can destroy on the main thread before a worker runs its queued
cluster-add callback and dereferences it, typically at startup. The factory now copies the state it
needs to create the load balancer instead of referencing the ``Cluster``.
