Added the ``envoy_dynamic_module_callback_bootstrap_extension_get_active_resource_names`` ABI getter
so dynamic-module bootstrap extensions can enumerate the names of the config objects Envoy currently
has active for a given resource kind. The kind
(``envoy_dynamic_module_type_bootstrap_active_resource_kind``) is passed as an input and selects
filter chains, clusters, transport socket matches or secrets, so future kinds are added without
changing the ABI signature. The Rust SDK exposes this as ``active_resource_names``.
