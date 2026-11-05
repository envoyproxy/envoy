The stats store now uses the explicit-tags logic by default: the tags of a stat are the ones
supplied by the code that creates it, and scope-level tags are propagated onto every stat, instead
of being re-extracted from the flat stat name with the default tag extraction regexes. This applies
only when the stats configuration carries no custom tag extraction rules, that is when
:ref:`stats_tags <envoy_v3_api_field_config.metrics.v3.StatsConfig.stats_tags>` is empty and
:ref:`use_all_default_tags <envoy_v3_api_field_config.metrics.v3.StatsConfig.use_all_default_tags>`
is left at its default of ``true``; any other stats configuration keeps the legacy tag extraction
logic. The flat stat names, the tag-extracted names and the tags are the same in both modes. The
mode is chosen once at startup and can be reverted by setting the runtime guard
``envoy.reloadable_features.enable_stats_explicit_tags`` to ``false``.
