Fixed a bug that unnecessary empty data chunks are processed by the filter chain. This
change can be reverted by setting the runtime guard
``envoy.reloadable_features.ext_proc_return_stop_iteration`` to ``false``.
