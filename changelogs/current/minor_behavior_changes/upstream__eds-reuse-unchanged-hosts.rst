EDS updates now reuse the existing host for an endpoint whose address and all other host fields are unchanged,
instead of building a new host that is then discarded. This lowers the main thread cost of an update for
large clusters. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.eds_reuse_unchanged_hosts`` to false.
