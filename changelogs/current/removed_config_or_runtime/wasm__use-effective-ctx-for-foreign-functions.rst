Removed the runtime guard
``envoy.reloadable_features.wasm_use_effective_ctx_for_foreign_functions`` and the legacy code path
it guarded. The ``set_envoy_filter_state`` and ``clear_route_cache`` Wasm foreign functions now
always resolve the effective context (``contextOrEffectiveContext``) instead of the current context.
