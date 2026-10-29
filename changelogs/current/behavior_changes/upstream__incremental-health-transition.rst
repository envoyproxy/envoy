A host health change now re-partitions only the priority that holds the host instead of every
priority. EDS clusters are unchanged. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.incremental_health_transition`` to ``false``.
