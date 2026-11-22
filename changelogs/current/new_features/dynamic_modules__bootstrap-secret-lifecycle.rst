Added dynamic-module bootstrap ABI callbacks for the secret lifecycle. A bootstrap extension opts in
via ``envoy_dynamic_module_callback_bootstrap_extension_enable_secret_lifecycle`` (``enable_secret_lifecycle``
in the Rust SDK) and is then notified through
``envoy_dynamic_module_on_bootstrap_extension_secret_add_or_update`` when a dynamic TLS certificate
secret is delivered or rotated, and through ``envoy_dynamic_module_on_bootstrap_extension_secret_removal``
when the last provider for it is destroyed. Available through the Rust SDK as ``on_secret_add_or_update``
and ``on_secret_removal``.
