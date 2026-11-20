Added runtime update notifications to the dynamic module bootstrap extension. A module watches a
runtime key by passing it to ``envoy_dynamic_module_callback_bootstrap_extension_enable_runtime_update``,
and Envoy then calls ``envoy_dynamic_module_on_bootstrap_extension_runtime_update`` with the key and
its new value whenever an RTDS update, a disk layer update or the admin ``/runtime_modify`` endpoint
changes it, including for runtime feature flags. The Rust SDK exposes these as
``enable_runtime_update`` of ``EnvoyBootstrapExtensionConfig`` and ``on_runtime_update`` of
``BootstrapExtensionConfig``. See
:ref:`Runtime Updates <config_bootstrap_extensions_dynamic_modules_runtime_update>`.
