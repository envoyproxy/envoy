Added support for forwarding the issuer of the client certificate in the
``x-forwarded-client-cert`` (XFCC) header via the new
:ref:`issuer <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.SetCurrentClientCertDetails.issuer>`
field of ``SetCurrentClientCertDetails``. When enabled, the ``Issuer`` key is added in text format and
the ``issuer`` field is added in JSON format. Defaults to disabled.
