The ``jwt_authn`` HTTP filter now strips every configured ``forward_payload_header`` and
``claim_to_headers`` header name from the request before applying rules. Previously those
headers were sanitized only inside the matched verifier, so paths that bypassed verification
(empty ``requires``, per-route ``disabled``, or CORS preflight bypass) could forward
client-supplied values upstream, and a request authenticated by one provider could retain
spoofed payload/claim headers configured on another provider. Guarded by
``envoy.reloadable_features.jwt_authn_sanitize_payload_headers_filter_wide`` (default
``true``).
