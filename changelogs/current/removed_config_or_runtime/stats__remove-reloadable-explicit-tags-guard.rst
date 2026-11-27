Removed runtime flag ``envoy.reloadable_features.enable_stats_explicit_tags``. The explicit stats
tags it enabled are now the default and can be reverted with
``envoy.restart_features.enable_stats_explicit_tags``.
