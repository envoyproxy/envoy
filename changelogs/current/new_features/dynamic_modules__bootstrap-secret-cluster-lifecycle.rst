Added dynamic-module bootstrap ABI callbacks for the secret and cluster lifecycle. A bootstrap
extension opts in via
``envoy_dynamic_module_callback_bootstrap_extension_enable_secret_lifecycle`` and
``envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle`` and is then notified
when dynamic TLS certificate secrets and clusters are added, updated or removed. The notifications
are marshalled onto the main thread before the module is invoked. Available through the Rust SDK as
``on_secret_add_or_update``/``on_secret_removal`` and
``on_cluster_add_or_update``/``on_cluster_removal``.
