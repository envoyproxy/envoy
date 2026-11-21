Added ``envoy_dynamic_module_callback_bootstrap_extension_subscribe_ecds`` and
``envoy_dynamic_module_on_bootstrap_extension_ecds_update``, with Rust SDK ``subscribe_ecds`` and
``on_ecds_update``, for :ref:`bootstrap extension configuration over ECDS
<config_bootstrap_extensions_dynamic_modules_ecds>`. Conflicting filter ECDS subscriptions on ADS
are refused.
