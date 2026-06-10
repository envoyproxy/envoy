Fixed a crash in the ``ring_hash`` and ``maglev`` load balancers when an EDS
ClusterLoadAssignment uses non-contiguous priorities (e.g. priorities ``0`` and
``5`` with ``1``-``4`` absent). ``TypedHashLbConfigBase::validateEndpoints`` now
skips the null host-list entries that ``PriorityStateManager`` leaves in the gap
slots instead of dereferencing them.
