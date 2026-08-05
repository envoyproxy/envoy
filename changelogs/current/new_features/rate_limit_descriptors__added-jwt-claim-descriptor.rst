Added a new :ref:`envoy.rate_limit_descriptors.jwt_claim
<envoy_v3_api_msg_extensions.rate_limit_descriptors.jwt_claim.v3.Descriptor>` rate limit descriptor
extension that extracts a named claim from a JWT found in an HTTP header and uses it as a
descriptor value. This is useful when JWT validation is performed elsewhere (e.g. by the
application, or by an upstream mTLS-authenticated service) and Envoy only needs to rate limit
based on the claim value. Note that this extension does not verify the JWT signature.
