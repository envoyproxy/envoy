http_inspector: fixed a stall in the ``http_inspector`` listener filter where a non-HTTP
connection whose first bytes look like a token followed by whitespace (for example an LDAP
bind request starting with a BER length byte of ``0x0c``) was passed to the deferred
``BalsaParser`` request-line validation and buffered indefinitely until the listener filter
timeout. The existing fast-fail check now also validates the request-target and HTTP-version
words of the request line, so such connections are classified as not-HTTP immediately. This
behavior can be temporarily reverted by setting the runtime flag
``envoy.reloadable_features.http_inspector_fast_fail_invalid_method_bytes`` to ``false``.
