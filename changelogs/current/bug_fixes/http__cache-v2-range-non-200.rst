Fixed the ``cache_v2`` filter incorrectly rewriting non-``200`` responses (for
example ``404``) to ``206`` or ``416`` when the request included a ``Range`` header.
Range is now applied only to ``200`` responses.
See `RFC 9110 Section 14.2 <https://httpwg.org/specs/rfc9110.html#field.range>`_.
