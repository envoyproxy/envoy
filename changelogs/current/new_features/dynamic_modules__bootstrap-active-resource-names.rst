Added the ``envoy_dynamic_module_callback_bootstrap_extension_get_active_resource_names`` ABI getter
so dynamic-module bootstrap extensions can enumerate the names of the config objects Envoy currently
has active for a given resource kind. The kind
(``envoy_dynamic_module_type_bootstrap_active_resource_kind``) is passed as an input and selects
filter chains, clusters, transport socket matches or secrets, so future kinds are added without
changing the ABI signature. The Rust SDK exposes this as ``active_resource_names``. For the filter
chain kind, an FCDS-delivered chain is reported only while an active listener's matcher references
it, so a name is returned only when the chain is both active and reachable, not merely committed in
the process-wide FCDS manager.
