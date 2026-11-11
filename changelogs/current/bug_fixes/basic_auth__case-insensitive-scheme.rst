Fixed the basic auth filter to match the ``Basic`` authentication scheme case-insensitively, as
RFC 9110 requires. Before this fix, the filter rejected credentials sent with a scheme such as
``basic`` or ``BASIC``. With
:ref:`allow_missing <envoy_v3_api_field_extensions.filters.http.basic_auth.v3.BasicAuth.allow_missing>`
set, the filter passed these requests through without authentication.
