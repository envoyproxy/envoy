Removed runtime flag ``envoy.reloadable_features.ext_proc_inject_data_with_state_update``. The filter
manager now tracks the observed end of stream per filter, so injecting data no longer needs to modify
the stream-level state.
