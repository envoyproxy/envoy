Fixed a crash when an upstream TLS context with a :ref:`custom certificate selector
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.custom_tls_certificate_selector>`
had an invalid ``common_tls_context``, for example an invalid ``key_log`` address range or an
unknown static secret. Envoy created the selector from the partly created configuration, so a
selector that built TLS contexts while it was created, such as the
``envoy.tls.certificate_selectors.on_demand_secret`` selector prefetching a secret that was already
available, read settings that were never set. Envoy now rejects the configuration before it creates
the selector.
