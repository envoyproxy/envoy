Added a listener writer to the dynamic module bootstrap extension. Once a module calls
``envoy_dynamic_module_callback_bootstrap_extension_enable_listener_writer``, it can add, update
and remove listeners with
``envoy_dynamic_module_callback_bootstrap_extension_listener_writer_add_or_update`` and
``envoy_dynamic_module_callback_bootstrap_extension_listener_writer_remove``, and
``envoy_dynamic_module_on_bootstrap_extension_listener_writer_update_done`` reports when each write
becomes active or why it failed. The Rust SDK exposes these as ``enable_listener_writer``,
``listener_writer_add_or_update`` and ``listener_writer_remove`` of
``EnvoyBootstrapExtensionConfig`` and ``on_listener_writer_update_done`` of
``BootstrapExtensionConfig``. See
:ref:`Listener Writer <config_bootstrap_extensions_dynamic_modules_listener_writer>`.
