Changed the ``jwt_authn`` filter's
:ref:`extract_only_without_validation <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.ExtractOnlyWithoutValidation>`
verification status header (default ``x-jwt-signature-verified``) to be set whenever a JWT is
present rather than only when a JWT fails verification. Because extract-only mode now never verifies
signatures and always forwards claims, the header is a token-presence signal: it is set to ``false``
whenever a JWT is forwarded (unverified) and is omitted only when no JWT is present. Previously it
was set only on the (rarely reached) verification-failure path. Still guarded by the runtime flag
``envoy.reloadable_features.jwt_authn_add_verification_status_header`` (default ``true``).
