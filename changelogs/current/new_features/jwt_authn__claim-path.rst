Added :ref:`claim_path
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtClaimToHeader.claim_path>` to
``claim_to_headers``, which names the claim to copy as an explicit list of path segments instead
of a single ``.``-joined string. Each segment is matched in full, so claims whose own names
contain dots are now addressable: the URL-namespaced claims issued by many OIDC providers, such as
``http://example.org/parent_token``, and nested ones such as ``c.d`` inside ``a.b``. Exactly one
of :ref:`claim_name
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtClaimToHeader.claim_name>` and
``claim_path`` must be set; a ``claim_to_headers`` entry setting both or neither is now rejected
at configuration load.
