Fixed a remote, unauthenticated denial-of-service in the Redis proxy filter: a single inline
command containing a literal ``x`` before a ``\xHH`` escape (e.g. ``SET key "x\x41"``) caused the
quoted-string decoder to mis-detect the escape marker and call ``std::stoul`` on a non-hex
string, throwing ``std::invalid_argument`` that escaped the filter and terminated the whole
Envoy process. The decoder no longer scans backwards for the escape marker; it now accumulates
the two hex digits of a ``\xHH`` escape in a dedicated buffer and converts them only once both
are present, matching Redis semantics. A truncated escape (one hex digit before the closing
quote) is emitted literally instead of crashing, and the out-of-bounds read when ``\x`` was the
first content of a quoted argument is eliminated.