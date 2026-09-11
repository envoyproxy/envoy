Integer-valued access log substitution commands are now rendered as exact integers in JSON
access logs rather than as shortest-round-trip doubles. Commands whose value is naturally an
integer -- such as ``%BYTES_SENT%``, ``%DURATION%`` and ``%COMMON_DURATION%`` -- previously went
through a ``double``, which is serialized in whichever of the plain and the exponent form is
shorter. Only the values that took the exponent form change: those are round numbers with
enough trailing zeros, the smallest being 100000, which was emitted as ``1e+05`` and is now
emitted as ``100000``. Values such as 123456, 1500000 and 86400000 were already emitted in full
and are unchanged, as are text (non-JSON) access logs. Consumers that parse JSON access logs
with a parser accepting either form see no difference, but a consumer relying on the exponent
form needs updating.
