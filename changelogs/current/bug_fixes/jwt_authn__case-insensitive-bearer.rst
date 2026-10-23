Fixed JWT extraction from the ``Authorization`` header to accept the ``Bearer`` authentication
scheme case-insensitively, including ``bearer`` and ``BEARER``. Custom value prefixes remain
case-sensitive.
