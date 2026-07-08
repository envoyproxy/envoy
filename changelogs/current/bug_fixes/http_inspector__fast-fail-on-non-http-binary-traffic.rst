Fixed a hang in the ``http_inspector`` listener filter when ``BalsaParser`` was
selected (the default since #44679, introduced by #37002). Because ``BalsaParser``
defers method-token validation until a line terminator is observed, non-HTTP
binary traffic that never sends ``\n`` (e.g. Java RMI/JRMP) would cause the
filter to buffer up to 64 KiB before closing, stalling the connection for the
duration of the downstream timeout. The filter now validates the request-line
prefix against the RFC 7230 §3.2.6 token character set before delegating to
the parser, restoring the fast-fail behavior the legacy ``http_parser``
provided. This behavior can be temporarily reverted by setting the runtime flag
``envoy.reloadable_features.http_inspector_fast_fail_invalid_method_bytes`` to
``false``.
