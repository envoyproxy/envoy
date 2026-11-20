Fixed a bug where a filter resuming iteration could end the stream early, dropping the remaining
body, when another filter earlier in the chain was still holding data that it later injected (for
example, three ext_proc filters processing the body in ``STREAMED`` mode). The filter manager can
now track whether the end of the stream has reached each filter. This is disabled by default and
can be enabled by setting the runtime guard
``envoy.reloadable_features.filter_manager_per_filter_buffer`` to ``true``. When enabled, the
runtime guard ``envoy.reloadable_features.ext_proc_inject_data_with_state_update`` has no effect.
