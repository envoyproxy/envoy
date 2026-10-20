Added support for implementing a custom TLS certificate selector as a dynamic module through the
:ref:`dynamic module TLS certificate selector
<envoy_v3_api_msg_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector>`
extension. For each handshake the module selects one of the configured certificates or lets Envoy
select one. With :ref:`allow_dynamic_certificates
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector.allow_dynamic_certificates>`
set, the module can also select certificates that it provides at runtime, and a handshake can wait
for one that is not provided yet. Modules can schedule work on the main thread and record metrics.
This works for both downstream and upstream TLS contexts.
