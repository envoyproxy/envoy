Bound token requests (:ref:`bound_jwt
<envoy_v3_api_field_extensions.filters.http.gcp_authn.v3.Audience.bound_jwt>` and
:ref:`bound_access_token
<envoy_v3_api_field_extensions.filters.http.gcp_authn.v3.Audience.bound_access_token>`) to the GCP
metadata server now use HTTP ``POST`` with the full PEM-encoded client certificate chain in the JSON
request body (``{"certificate_chain": "<PEM>"}``), replacing the deprecated HTTP ``GET`` request with
the ``bindCertificateFingerprint`` query parameter.

