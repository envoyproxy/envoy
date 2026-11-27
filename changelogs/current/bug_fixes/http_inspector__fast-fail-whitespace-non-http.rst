Fixed a bug in ``http_inspector`` where non-HTTP connections whose first non-token
byte was whitespace other than a single space (such as LDAP simple bind payloads
with form feed ``\f`` length bytes, tabs, or vertical tabs) stalled connection
processing under ``BalsaParser``. The filter now strictly validates that the HTTP
method is followed by a single space ``SP`` per RFC 9112 and checks that the request
target does not contain binary control characters prior to CRLF, fast-failing non-HTTP
traffic. This behavior can be temporarily reverted by setting the runtime flag
``envoy.reloadable_features.http_inspector_fast_fail_non_sp_whitespace`` to ``false``.
