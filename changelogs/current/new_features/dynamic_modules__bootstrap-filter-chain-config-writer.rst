Added a filter chain config writer to the dynamic module bootstrap extension. It writes the filter
chains that the listeners with ``fcds_config`` find by name. Once a module calls
``envoy_dynamic_module_callback_bootstrap_extension_enable_filter_chain_config_writer``, it can add,
update and remove filter chains with
``envoy_dynamic_module_callback_bootstrap_extension_filter_chain_config_writer_add_or_update`` and
``envoy_dynamic_module_callback_bootstrap_extension_filter_chain_config_writer_remove``, and
``envoy_dynamic_module_on_bootstrap_extension_filter_chain_config_writer_update_done`` reports when
each write becomes active or why it failed. The Rust SDK exposes these as
``enable_filter_chain_config_writer``, ``filter_chain_config_writer_add_or_update`` and
``filter_chain_config_writer_remove`` of ``EnvoyBootstrapExtensionConfig`` and
``on_filter_chain_config_writer_update_done`` of ``BootstrapExtensionConfig``. See :ref:`Filter
Chain Config Writer <config_bootstrap_extensions_dynamic_modules_filter_chain_config_writer>`.
