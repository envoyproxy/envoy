Added string values to the runtime update notifications of the dynamic module bootstrap extension.
A watched runtime key whose value is a non-empty string that is neither a number nor a boolean is now
reported through the new ``envoy_dynamic_module_on_bootstrap_extension_runtime_string_update`` hook,
instead of counting as unset, and the new ``envoy_dynamic_module_callback_get_runtime_string``
callback reads a runtime value as its text. The Rust SDK adds ``RuntimeValue::String`` and
``get_runtime_string``. See
:ref:`Runtime Updates <config_bootstrap_extensions_dynamic_modules_runtime_update>`.
