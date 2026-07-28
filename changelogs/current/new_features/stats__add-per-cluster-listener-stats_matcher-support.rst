Added per-cluster and per-listener ``stats_matcher`` configuration that overrides the bootstrap
:ref:`stats_config <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.stats_config>` matcher for
the specific cluster or listener. When this field is configured, legacy
``envoy.stats_matcher`` metadata is ignored.
