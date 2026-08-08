Fixed a bug where stats created with programmatic tags (via ``*FromStatNameWithTags``) lost their
tags across a hot restart. Such stats inline their tag values into the stat name, but the hot
restart stat merge re-created them from the name alone with no tags, so after a restart the tag
values collapsed into the metric name and the labels became empty (for example as exported to
Prometheus). The parent now transmits the tag-extracted name and tag values for these stats, and
the child re-creates them with identical labels. This behavior can be temporarily reverted by
setting the runtime flag ``envoy.reloadable_features.hot_restart_propagate_stat_tags`` to
``false``.
