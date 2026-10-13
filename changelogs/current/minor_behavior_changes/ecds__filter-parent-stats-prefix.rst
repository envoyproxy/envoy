The filter factory of an HTTP filter configured by the extension config discovery service (ECDS)
now uses the stats prefix of its parent, e.g. ``http.<stat_prefix>.`` of the HTTP connection
manager, rather than ``extension_config_discovery.<stat_prefix>.<extension_config_name>.``. So the
filter emits the same stats as a statically configured one. The stats of the ECDS subscription
are still rooted at ``extension_config_discovery.<stat_prefix>.<extension_config_name>``. This
behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.ecds_filter_use_parent_stats_prefix`` to ``false``.
