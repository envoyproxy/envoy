Fixed a bug where a ``claim_to_headers`` entry whose :ref:`claim_name
<envoy_v3_api_field_extensions.filters.http.jwt_authn.v3.JwtClaimToHeader.claim_name>` contains
dots but is not a nested path, such as the URL-namespaced claim
``http://example.org/parent_token``, was silently dropped. ``claim_name`` is still split on ``.``
and resolved as a path into nested JSON objects first, and only if that fails is the full string
now tried as a literal top-level claim name. The nested path therefore keeps taking precedence and
no configuration which resolves today changes behavior. A claim which cannot be resolved at all is
now also logged at debug level instead of being dropped without any trace. This behavioral change
can be reverted by setting the runtime guard
``envoy.reloadable_features.jwt_authn_literal_claim_name_fallback`` to ``false``.
