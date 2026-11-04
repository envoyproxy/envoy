Optimized Scoped RDS (SRDS) configuration update processing by avoiding full-table copies and proto
cloning during conflict detection, coalescing scope removals and additions into a single worker-thread
update, and pre-reserving hash table capacities for net-new scopes. Guarded by runtime guard
``envoy.reloadable_features.srds_fast_config_update`` (disabled by default).
