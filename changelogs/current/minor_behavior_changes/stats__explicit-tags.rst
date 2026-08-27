Added the runtime guard ``envoy.reloadable_features.enable_stats_explicit_tags`` (default
``false``). When set to ``true`` and the stats configuration carries no custom tags (empty
:ref:`stats_tags <envoy_v3_api_field_config.metrics.v3.StatsConfig.stats_tags>` and
:ref:`use_all_default_tags <envoy_v3_api_field_config.metrics.v3.StatsConfig.use_all_default_tags>`
left at its default of ``true``), the stats store uses the tags supplied by the calling code (the
explicit-tags logic) and propagates scope-level tags onto every stat, instead of re-parsing the
flat stat name. The guard is evaluated once at startup. There is no visible change to users while
the guard remains ``false``.
