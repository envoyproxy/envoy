Added :ref:`allow_default_tag_overrides
<envoy_v3_api_field_config.metrics.v3.StatsConfig.allow_default_tag_overrides>` to allow custom tag
extractors in :ref:`stats_tags <envoy_v3_api_field_config.metrics.v3.StatsConfig.stats_tags>` to take
precedence over the built-in default tag extractors with the same ``tag_name``, instead of the
default taking precedence. This makes it possible to override individual default Envoy tags (for
example ``envoy.cluster_name``) while keeping :ref:`use_all_default_tags
<envoy_v3_api_field_config.metrics.v3.StatsConfig.use_all_default_tags>` enabled.
