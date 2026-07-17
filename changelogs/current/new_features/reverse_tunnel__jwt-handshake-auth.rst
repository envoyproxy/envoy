Added experimental inline JWT authentication for the reverse tunnel handshake via the new
:ref:`jwt_validation <envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.ReverseTunnel.jwt_validation>`
field on the ``envoy.filters.network.reverse_tunnel`` filter. When configured, the bearer token
carried in the handshake request is verified (signature, issuer, audiences, and ``exp``) against an
inline ``local_jwks`` before the connection is accepted and its socket registered, so a forged or
expired token cannot establish a usable reverse tunnel. A ``jwt_validation`` block requires an
``issuer``, and tokens without an ``exp`` claim are rejected. Verified claims are published as dynamic
metadata so the existing ``validation`` block can bind a claimed identifier to a verified claim via
``%DYNAMIC_METADATA(namespace:claim)%``. Only inline JWKS (synchronous verification) is supported;
remote JWKS fetching is not yet implemented.
