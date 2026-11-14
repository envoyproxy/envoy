Fixed a bug where a filter resuming iteration could end the stream early, dropping the remaining
body, when another filter earlier in the chain was still holding data that it later injected (for
example, three ext_proc filters processing the body in ``STREAMED`` mode). The filter manager now
tracks whether the end of the stream has reached each filter. When enabled, the runtime guard
``envoy.reloadable_features.ext_proc_inject_data_with_state_update`` has no effect. This change can
be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_per_filter_observed_end_stream`` to ``false``.
