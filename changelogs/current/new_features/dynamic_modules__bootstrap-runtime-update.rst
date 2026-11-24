Added runtime update notifications to the dynamic module bootstrap extension. From
``envoy_dynamic_module_on_bootstrap_extension_server_initialized``, an extension watches a runtime
key by passing it to ``envoy_dynamic_module_callback_bootstrap_extension_enable_runtime_update``,
and Envoy then calls ``envoy_dynamic_module_on_bootstrap_extension_runtime_bool_update``,
``envoy_dynamic_module_on_bootstrap_extension_runtime_int_update`` or
``envoy_dynamic_module_on_bootstrap_extension_runtime_number_update`` on the extension, depending on
the type of the value, whenever an RTDS update, a disk layer update or the admin
``/runtime_modify`` endpoint changes it, including for runtime feature flags. The Rust SDK exposes
these as ``enable_runtime_update`` of ``EnvoyBootstrapExtension`` and ``on_runtime_update`` of
``BootstrapExtension``, which receives an optional ``RuntimeValue``. See
:ref:`Runtime Updates <config_bootstrap_extensions_dynamic_modules_runtime_update>`.
