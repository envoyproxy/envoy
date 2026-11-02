Added support for implementing a custom TLS certificate selector as a dynamic module through the
:ref:`dynamic module TLS certificate selector
<envoy_v3_api_msg_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector>`
extension. For each handshake the module selects one of the configured certificates or lets Envoy
select one. With :ref:`allow_dynamic_certificates
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector.allow_dynamic_certificates>`
set, the module can also select certificates that it provides at runtime, and a handshake can wait
for one that is not provided yet. Modules can schedule work on the main thread and record metrics.
This works for both downstream and upstream TLS contexts. The dynamic certificates that are served
are listed by the ``/certs`` admin endpoint and count in the
``server.days_until_first_cert_expiring`` and ``server.seconds_until_first_ocsp_response_expiring``
statistics, and the selector emits
:ref:`statistics <config_tls_certificate_selectors_dynamic_modules>` about them.
