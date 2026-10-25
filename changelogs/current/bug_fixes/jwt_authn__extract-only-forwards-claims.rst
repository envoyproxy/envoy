Fixed a bug in the ``jwt_authn`` filter where the
:ref:`extract_only_without_validation <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.ExtractOnlyWithoutValidation>`
requirement did not actually forward JWT claims when the token could not be verified. Claim
extraction (``forward_payload_header``, ``claim_to_headers``, ``header_in_metadata`` and
``payload_in_metadata``) only ran on the signature-verification-success path, so a request whose
JWT was expired, had a bad/unknown signing key, or whose issuer had no usable JWKS was allowed
through with no claims forwarded at all -- the opposite of the mode's documented
decode-and-forward-without-validation contract. The filter now decodes any parseable JWT and
forwards its claims without any signature, expiration, audience or issuer validation, and never
inserts the unverified token into the JWT cache. This behavior is guarded by the runtime flag
``envoy.reloadable_features.jwt_authn_extract_only_forwards_unverified_claims`` (default ``true``);
setting it to ``false`` restores the previous behavior.
