Added dynamic-module bootstrap ABI callbacks for the secret lifecycle. A bootstrap extension opts in
via ``envoy_dynamic_module_callback_bootstrap_extension_enable_secret_lifecycle`` and is then
notified when a dynamic TLS certificate secret is delivered or rotated, and when the last provider
for it is removed. Available through the Rust SDK as ``on_secret_add_or_update`` and
``on_secret_removal``.
