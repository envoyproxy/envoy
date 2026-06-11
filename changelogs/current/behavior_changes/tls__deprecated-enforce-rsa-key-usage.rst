The :ref:`enforce_rsa_key_usage <envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.enforce_rsa_key_usage>`
configuration option has been deprecated and ignored. Envoy now always enforces the ``keyUsage`` extension in peer
certificates. Configurations setting this option to ``false`` will no longer have any effect and enforcement will be used.
