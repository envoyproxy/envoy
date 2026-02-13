Fixed a bug to support two ext_proc filters configured in the chain. This change can be
reverted by setting the runtime guard
``envoy.reloadable_features.ext_proc_inject_data_with_state_update`` to ``false``.
