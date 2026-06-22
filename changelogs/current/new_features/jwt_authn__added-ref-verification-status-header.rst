Added :ref:`verification_status_header
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.ExtractOnlyWithoutValidation.verification_status_header>`
to the ``ExtractOnlyWithoutValidation`` requirement. When a JWT is present in the request but fails
signature verification, the named request header (default ``x-jwt-signature-verified``) is set to
``false`` so downstream filters (RBAC, ext_authz) can distinguish forwarded-but-unverified claims
from validated ones. The header is not set on a successfully verified JWT or when no JWT is present.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.jwt_authn_add_verification_status_header`` to ``false``.
