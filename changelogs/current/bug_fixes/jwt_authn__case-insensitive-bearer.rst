Fixed JWT extraction from the ``Authorization`` header to accept the ``Bearer`` authentication
scheme case-insensitively, including ``bearer`` and ``BEARER``. Custom value prefixes remain
case-sensitive.
This behavior can be disabled by setting the runtime guard
``envoy.reloadable_features.jwt_authn_case_insensitive_bearer`` to ``false``.
