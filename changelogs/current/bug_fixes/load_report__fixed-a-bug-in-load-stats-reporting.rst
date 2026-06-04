Fixed a bug in load stats reporting where reports were dropped if only custom metrics or completed
requests were present in a reporting interval. This behavioral change can be reverted by setting
the runtime guard ``envoy.reloadable_features.report_load_for_non_zero_stats`` to ``false``.
