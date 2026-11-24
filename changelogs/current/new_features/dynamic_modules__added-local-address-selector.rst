Added a dynamic modules :ref:`local address selector
<envoy_v3_api_msg_extensions.local_address_selectors.dynamic_modules.v3.DynamicModuleLocalAddressSelector>`
that lets a module pick the source address of each upstream connection, defer to the default
selection, or fail the connection. Local address selector factories can now receive their typed
config and the server context by overriding ``createLocalAddressSelectorWithConfig``.
