Added configuration snapshots for bootstrap dynamic modules, with provider selection, resource
name filtering and sensitive-field redaction, through
``envoy_dynamic_module_callback_bootstrap_extension_config_snapshot_new``,
``envoy_dynamic_module_callback_bootstrap_extension_config_snapshot_serialize`` and
``envoy_dynamic_module_callback_bootstrap_extension_config_snapshot_delete``. The Rust SDK exposes
``EnvoyBootstrapExtensionConfig::get_config_snapshot`` and
``EnvoyBootstrapExtensionConfigSnapshot::into_json``; dropping the snapshot releases it. See
:ref:`configuration snapshots <config_bootstrap_extensions_dynamic_modules_config_snapshots>`.
